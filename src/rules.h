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
    /* A saved game's layout (party.c's cok_saved_game_read and write;
     * 4b6d:1b34 and 22de, dkk 46b2:1ef0 and 2808): the ECL file; the
     * variables 0x4b00-0x4eff, 0x7c00-0x7fff and 0x7a00-0x7bff, each
     * address as many bytes as it holds (ecl.variable_bytes: words in
     * Champions, bytes in Death Knights); the party's square, facing, wall
     * ahead and square byte; the last mode and the mode; walls entries of
     * the wall table, each its record and slot as words (Champions'
     * DS:6d8a, slots 1-3: 3; dkk DS:c078, slots 0-3: 4); the party's size
     * and eight names, string[40]s. walls_restored: the entries the loader
     * loads again, from the first (4b6d:1b34 all three; dkk 46b2:1ef0 the
     * first alone, 6d21:0ec5 with DS:c07a and c078). */
    uint8_t walls, walls_restored;
    /* The byte the roster's list leaves a record out by, at 0x80 and up,
     * a cok_field and its element (4b6d:0453: the jewelry's low byte,
     * COINS 6, where the NPC flag was surely meant; dkk 46b2:0342 the NPC
     * flag, DS:57a). */
    uint8_t roster_flag, roster_flag_index;
    /* Whether the loader leaves the saved last mode the last mode (dkk
     * 46b2:1ef0 reads it into DS:8c4f and keeps it), rather than making the
     * saved mode the last (4b6d:1b34: DS:4b4a = 4b49). */
    bool keep_last_mode;
    /* The bank vault (dkk 46b2:2e1d reads it, 302d writes it):
     * <vault><letter>.<vault_extension> beside the saved game of that
     * letter, and the copy its menu leaves until the game is saved
     * (vault_temp, a whole name); NULL: the game has none (Champions). */
    const char *vault, *vault_extension, *vault_temp;
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
    /* Whether the missiles' icons (COMSPR) are recoloured as a CPIC's are
     * outside CGA mode (DS:SAME_COLOURS to BLACK_COLOURS: 8 to 0) wherever
     * they are loaded (dkk 7090:016f: every name but CHEAD, CBODY and
     * ICON), rather than only the CPIC's (6d21:01d0). */
    bool missiles_recoloured;
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
    /* What the port's handler takes as constants where the games' differ,
     * else 0: COK_CAST_DICE's damage, count dice of sides, of type;
     * COK_CAST_ENLARGE's value 1 for a strength of 22 at every caster
     * level but 1-9 (dkk 5dc5:2100; Champions' 21d7 at 10 and 11 only);
     * COK_CAST_LIGHTNING_BOLT's value 1 for its dice counted (dkk 5dc5:37f8
     * rolls through 638c:1233, keeping the count in DS:be71);
     * COK_CAST_CHARM_PERSON's value 1 for an elf (+0x20 7) without the
     * creature flag 2 unaffected, rather than a monster of some kind (dkk
     * 5dc5:2044; Champions' 20f8: creature flags above 1);
     * COK_CAST_STRENGTH's value 1 for 1d8 to every class of
     * characters.fighter_classes, not a fighter's alone (dkk 5dc5:2bc9);
     * COK_CAST_STINKING_CLOUD's value 1 for Cloud Kill's cloud (dkk
     * 5dc5:2934 for a spell but 0x22: nine cells, fight.pool_tile);
     * COK_CAST_FIREBALL's value 1 for the caster level added to the damage
     * (dkk 5dc5:3241, Delay Blast Fireball's 0x6c); COK_CAST_REMOVE_CURSE's
     * value the effect it cures, where not COK_EFFECT_CURSED (dkk 5dc5:311b:
     * 0x2b); COK_CAST_FEAR's value 1 for one in the cone frightened only
     * while it holds COK_EFFECT_TERROR after (dkk 5dc5:4566, 65cd:2687);
     * COK_CAST_FIRE_SHIELD's value 1 for Death Knights' (dkk 5dc5:468f):
     * the flame by index from the menu (6a69:0f1b, casting.flame_type) or
     * for the computer by 1d2, either saying text, the abort question
     * casting's. */
    uint8_t value, count, sides, type;
    /* The words the handler says where they are not Champions' (the
     * handler's own, as 5b04's routines hold them), else NULL: dkk's
     * strings in its data segment ("is fire-resistant", DS:204c); for
     * COK_CAST_SAYING and COK_CAST_MISSILES, all they say. */
    const char *text;
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
    /* Who may use a spell's class (5b04:0083; dkk 5dc5:0078): clerics'
     * also a paladin of usable_paladin and up (dkk 9; 0 none), druids' a
     * ranger of usable_ranger and up (7; dkk 8), and magic-users' only a
     * magic-user or a ranger of 9 and up where usable_mage_class (dkk). */
    uint8_t usable_paladin, usable_ranger;
    bool usable_mage_class;
} cok_rules_spells;

/* One effect id's handler (effect.c): the port's, a cok_effect_handler
 * (spells.h), and the original's routine, its offset (Champions' 3f44 or
 * 5b04); or, where the port does not carry the game's routine out yet,
 * unported, citing it: running it fails. */
typedef struct {
    uint16_t address;
    uint8_t handler;
    const char *unported;
    /* What the routine takes as a constant where one handler serves
     * several ids (COK_FX_CANCEL's effect), or 0; and the words it says
     * where the games word them differently (COK_FX_STENCH's of one
     * affected), or NULL. */
    uint8_t value;
    const char *text;
    /* Whether the game's routine does nothing outside a battle (dkk
     * DS:8c4e 5: 0x1a, 0x23, 0x25, 0x69, 0x72, 0x73); Champions' run in any
     * mode. */
    bool battle;
} cok_rules_effect;

/* A breath that dkk 5dc5:51cc carries out (COK_FX_DRAGON_BREATH), by
 * effect id: its damage type, the cone's width and length (5dc5:175d), the
 * spell whose computer's pick aims it (column 0 of the handler table,
 * DS:c150), and what "breathes ". */
typedef struct {
    uint8_t id, damage_type, width, length, spell;
    const char *what;
} cok_rules_breath;

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
    /* Display (4888:17ae; dkk 4381:15aa): the ids it names by the spell
     * that adds them, 0-ended, and the texts of others; it shows no other.
     * Its headings' colour is the levels section's. */
    const uint8_t *display_spells;
    const cok_rules_effect_text *display_texts;
    unsigned display_text_count;
    /* The effect whose holder's saving throw rolls 1 (dkk 638c:10f8: 0x39),
     * or 0; whether the saving throw's moon of a mage's order of magic is
     * read at 0x4b00 + the byte order + 0xf8 (60f4:113a's sum wraps: var
     * COK_VAR_AREA) rather than at 0x4cf8 + order (dkk 638c:10f8 sums a
     * word: var COK_VAR_ORDER_MOON, the moon's phase). */
    uint8_t save_fail_id;
    bool save_moon_wraps;
    /* The caster level (6346:29fe; dkk 65cd:2c33): the levels a knight's
     * cleric spells and a ranger's mage spells are less than its knight
     * and ranger levels (8 and 8; dkk 0 and 0); and a spell whose caster
     * holding an effect casts it at a level of its own (dkk 0x2f, 0x4e,
     * 15; 0 none). */
    uint8_t caster_knight_less, caster_ranger_less;
    uint8_t caster_spell, caster_effect, caster_spell_level;
    /* What one an effect does not take hold of "is" (60f4:20f7, CS:20e9;
     * dkk 638c:1f86, DS:1d0f). */
    const char *unaffected;
    /* The breaths of COK_FX_DRAGON_BREATH (dkk 5dc5:51cc); none in
     * Champions. */
    const cok_rules_breath *breaths;
    unsigned breath_count;
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
    /* The other game's roster and saved games are read from the directory
     * the configuration's fifth line names (dkk DS:99a4; the port's
     * --import): its roster by the NPC flag (dkk 46b2:0342 tests the
     * source's own, DS:57a: Champions' +0xe7), its saved game's party and,
     * of its variables, COK_VAR_SAVINGS alone (46b2:1ef0). */
} cok_rules_import;

/* The game's characters' routines that differ. */
typedef struct {
    cok_stats_routine stats;
    cok_levels_routine levels;
    /* The import of another game's characters, or NULL. */
    const cok_rules_import *import;
    /* The experience table's row of a paladin's levels (dkk 65cd:382c:
     * 10), or 0 where the row routine has none for a paladin (6346:371e). */
    uint8_t paladin_row;
    /* The classes whose levels count as a fighter's, as bits by class:
     * exceptional strength above 18 (60f4:1743) and a fighter's hit points
     * by constitution (60f4:1628): fighters, rangers and knights (bits 2,
     * 4, 7); dkk 638c:16d8 and 15e0 paladins too (bit 3). */
    uint8_t fighter_classes;
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

/* A line of text at a cell, in a colour on black (the credits). */
typedef struct {
    uint8_t x, y, fg;
    const char *text;
} cok_rules_text_line;

/* One of the title's pictures: TITLE record drawn opaque at x units and y
 * rows of cells (127f:10e7; dkk 123c:10fe), the screen cleared first where
 * clear (dkk 14e4:0a37), then seconds waited (2f55:0000), 0 for none. */
typedef struct {
    uint8_t record, x, y;
    bool clear;
    uint8_t seconds;
} cok_rules_title_picture;

/* The game's front end (start.c, roster.c, camp.c's Save): the title, the
 * start menu's words, and what its commands do where the games differ. */
typedef struct {
    /* The title (2f55:063c; dkk 35c6:051c): its pictures in turn, a key
     * pressed while one waits ending them; then the credits (2f55:0267;
     * dkk 35c6:026e: the cleared screen, the credits' frame and these
     * lines) for credits_wait hundredths of a second, unless a key ended
     * the pictures and credits_always is false. */
    cok_rules_title_picture pictures[4];
    unsigned picture_count;
    const cok_rules_text_line *credits;
    unsigned credit_count;
    uint16_t credits_wait;
    bool credits_always;
    /* A new game's first block (2fd3:3c28: 0x24; dkk 3632:2e06: 0x10),
     * and the demonstration's ECL file and block (1000:0134 and 2fd3:3c28:
     * 2 and 0x39; dkk entry and 3632:2e06: 5 and 2, an ECL5 it does not
     * ship). */
    uint8_t first_block, demo_file, demo_block;
    /* The start menu's items (DS:0878; dkk DS:0cf8: 13 string[40]s with a
     * flag after each, 0x2a bytes apart), its list's prompt and its word
     * (4def:01b4's; dkk DS:2fad and 31d6). */
    const char *items[13];
    const char *prompt, *select;
    /* By item, the game's routine where the port does not carry it out
     * yet, citing it ("dkk 49cb:05c7"), or NULL: choosing it stops the run
     * (COK_ECL_UNPORTED). */
    const char *unported[13];
    /* Pick Character's prompt and menu (546c:36cc; dkk 580d:3067). */
    const char *pick_prompt, *pick_menu;
    /* Drop Character's words (4def:2668; dkk 49cb:2394), each %s the
     * name: the two questions, the farewells of one who cannot act and of
     * one who can, and No's. */
    const char *drop, *drop_sure, *dumped, *farewell, *relief;
    /* Exit to DOS's questions (4def:055a; dkk 49cb:010b's). */
    const char *quit, *unsaved;
    /* The colour of the questions' prompts, Drop's, Exit's, Overwrite's and
     * camp's after Save:
     * Champions' callers give 67b5:177f yellow (14); dkk 6a69:2048 shows
     * its prompt as every menu does, light magenta (13). */
    uint8_t ask_colour;
    /* Load's and Save's (4b6d:1b34, 22de; dkk 46b2:1ef0, 2808): the
     * letter's prompt, the warning for a party of none, and where the
     * game imports another's, "from where" over the games (DS:306c and
     * 3337: the game's own, the other's, Exit; Add's DS:1393). */
    const char *load_prompt, *save_prompt, *save_warning;
    const char *load_from, *add_from, *from_items;
    /* Saving to the roster (4b6d:0bed; dkk 46b2:0abc): %s the file's
     * name. */
    const char *overwrite, *new_name;
    /* Add Character to Party (4def:37a2; dkk 49cb:343c): its list's prompt
     * and word, the rangers' message, whether paladins turn away evil and
     * the evil paladins (dkk has neither rule), whether a space before any
     * choice leaves it (4def:3ae7's count never set; dkk has none), whether
     * "Loading...Please Wait" shows as a character is read (4b6d:1283; dkk
     * 49cb:343c clears row 24 alone) and whether one turned away loses its
     * "* " (dkk keeps it). */
    const char *add_prompt, *add_select, *rangers;
    bool paladins, add_space_leaves, add_loading, add_unmarks;
    /* Whether a Drop or Remove that empties the party ends the game there
     * (dkk 49cb:010b: 4f9a:08ad and DS:8c5b, the adventure starting over
     * once the menu returns); whether Exit's No saves and then quits (dkk)
     * rather than staying in the menu (4def:055a); whether Begin's mode is
     * the last mode, which the menu sets from the mode it was entered with
     * and a load from the saved game's (dkk DS:8c4f), rather than the mode
     * the menu was entered with (4def:01b4's [bp-0x10]). */
    bool reset_when_empty, quit_after_save, begin_last_mode;
    /* Whether PROGRAM 0's redraw on the overland map marks the party on
     * it again (dkk 3632:23dd: 6997:0000; 2fd3:3473 does not). */
    bool program_marks;
    /* The wall table's entry the end of a game sets to (0, 1), the next
     * ones -1 (3e99:0843: the first; dkk 4f9a:08ad, Champions' 1-based loop
     * over its 0-based table: the second, the first kept). Startup sets
     * the first (3e99:005b, dkk 4f9a:00e1). */
    uint8_t reset_wall;
} cok_rules_start;

/* The wilderness battlefield's builder (combat.c), where the games' differ:
 * each the port's routine of that game's original. */
typedef enum {
    /* 3cb2:0fc8: plain floor 0x17; a river by the terrain's flags 0x20 and
     * 0x10 (3cb2:0a00), trees (0b0b), scatter (0e4a). */
    COK_WILDERNESS_CHAMPIONS,
    /* dkk 410b:107e: plain floor 0x45; a river where var COK_VAR_RIVER is
     * set (410b:0a4c), trees (0beb) and scatter (0f00) of its own tiles. */
    COK_WILDERNESS_DEATH_KNIGHTS,
} cok_wilderness_routine;

/* The encounter and the battle's frame where the games differ (monster.c,
 * combat.c, round.c, treasure.c's end of combat): COMBAT's branches, the
 * show at distance, the battle around its rounds, the battlefield's setup
 * and placement, the end of combat, and the routines a game's battle has
 * that the port does not carry out for it yet (cited: reaching one stops
 * the run, COK_ECL_UNPORTED). */
typedef struct {
    /* COMBAT (2fd3:191c; dkk 3632:16ec) with no monsters loaded: whether
     * the vault's menu (var COK_VAR_VAULT 1) comes first, before a shop
     * and the temple (the shops section's); and the game's vault menu
     * where the port does not carry it out for it yet (dkk 3948:000e), or
     * NULL. */
    bool vault;
    const char *vault_unported;
    /* The show at distance (3775:0575; dkk 3c1e:0515): whether a sprite or
     * close-up id of 0xff shows none, and a close-up shows at any distance
     * when there is no sprite (dkk); Champions' has neither rule, and its
     * close-up may be a portrait (var COK_VAR_PORTRAIT). */
    bool show_none;
    /* The battle (3995:0172; dkk 3e05:00ec): whether leadership (dkk
     * 5340:4acf) comes before the setup, with its words (the first line,
     * then name, leads, name and the end); whether after each turn and
     * each blow the dead explode (60f4:2375; dkk has none); whether after
     * each turn a side gone or the round limit ends the round there, once
     * a battle (dkk 3e05:00ec's [bp-1]), rather than only at the end of the
     * round. The aging after it is the levels section's. */
    bool leadership;
    const char *leadership_text, *leads, *leads_end;
    bool explode, ends_in_round;
    /* The setup (3cb2:1c58; dkk 410b:1e41): its line on row 24 (Champions'
     * in 1521's code, CS:1c45; dkk DS:2a07); the tiles from the 3D area's
     * and the wilderness's archives (3cb2:1029: 0x19 and 0x21; dkk
     * 410b:10d7: 0x19 and 0x26) and from the random ones' (archives.random,
     * 6 at 0x22; none where the stem is NULL), and whether a set already
     * loaded is not loaded again (dkk DS:b58c, b58d); the wilderness's
     * builder (a cok_wilderness_routine); the dungeon's map before it is
     * built: the random tiles' record 1 as the heap left it (3cb2:1029's
     * GetMem), or, where dungeon_fill is set, cleared and filled with
     * dungeon_floor (dkk 410b:10d7 and 0892: 0x16); whether a door on the
     * north-west corner's west edge, and beyond the north-east one's north,
     * is drawn as a wall's corner is (dkk 410b:04d8, 06ae); whether the
     * setup clears Magic On (DS:7198) and the dead that explode (DS:6b45,
     * 6b95; dkk neither). */
    const char *begins;
    uint8_t dungeon_tiles, wilderness_tiles;
    bool tiles_kept;
    uint8_t wilderness;
    bool dungeon_fill;
    uint8_t dungeon_floor;
    bool door_corners;
    /* The placement (3cb2:17f7; dkk 410b:1aa4 and 18c2): the entries whose
     * sizes it clears first, 1 to placement_clears (255, into the screen
     * positions; dkk 72); whether SETUP MONSTERS' ambush directions
     * (game->ambush, dkk DS:c4cc) split the enemy's side among them, each
     * enemy facing from the next, and each record's combat facing is set
     * here; the enemy's distance where it is 0 and the facing is not the
     * party's (dkk 1), and in the demonstration (dkk 2; 0 none); whether a
     * body's cell keeps the cell under it rather than the map's (dkk
     * 6f03:152e: under a body already there, the body's); and whether a
     * record of status 9 is taken off the map (3cb2:17f7; dkk none). */
    uint8_t placement_clears;
    bool ambush;
    uint8_t ambush_distance, demo_distance;
    bool body_under, status_9;
    /* The end of combat (351b:1968; dkk 3a37:1318): whether the weapons
     * lost in combat come back first (351b:185f) and the NPCs take their
     * shares (351b:1618; dkk neither); the effects party records lose
     * (351b:0574: DS:0390, 11 of them; dkk 3a37:0506: DS:455d, 19), 0-ended;
     * whether the award also sets whether one may
     * train (dkk 3a37:02fa with 65cd:38c7, COK_FIELD_MAY_TRAIN); the results' words
     * (351b:0b23; dkk 3a37:09b7): fled, lost, won, found treasure, each
     * receives, experience points, and where the game says it after the
     * experience, found no treasure (dkk; NULL none), and the bank's two
     * lines (NULL none); the key the results wait for, its word (67b5:03e2
     * with 15, 15, 15; dkk 6a69:0f1b); whether the treasure menu comes only
     * when the pool holds anything (dkk), and where the port does not
     * carry the game's treasure menu out yet (dkk 3a37:0eed), or NULL;
     * a destroyed party's words and its key's. */
    bool weapons_back, shares;
    const uint8_t *combat_effects;
    bool may_train;
    const char *fled, *lost, *won, *found, *receives, *points, *found_none;
    const char *withdrawal, *fee;
    const char *results_key;
    bool treasure_if_any;
    const char *treasure_unported;
    const char *rejoice, *rejoice_key;
    /* Whether a party destroyed while a block is entered (its load
     * vector, and the blocks a NEWECL there enters) still gets the
     * adventure's menu once, its choice not acted on, before the game
     * ends: dkk 3632:2e06 runs the load vector (2cd5 with DS:8c46) and
     * reads the menu (4307:0257) before it tests DS:8c5a, the party
     * destroyed. Champions' 2fd3:3c28 has the same shape (3add with
     * DS:4b41, then 475c:09ec before DS:4b57), not checked against the
     * original: no, the port's game there ending at once. */
    bool killed_command;
} cok_rules_combat;

/* The fight's words that differ (round.c, ai.c, attack.c, arena.c,
 * command.c): Champions' in its code segments, Death Knights' in its data
 * segment, most in lower case. */
typedef struct {
    /* The attack's message (432f:033e; dkk 5340:0230, DS:2d0d-2d8d): its
     * header (Attacks, mode 2's, mode 3's), "(from behind)", the hit's
     * words (mode 3's, else hitting, the number, point or points and the
     * damage), the miss's; one casting losing its spell, one dropping, one
     * dying and one killed (DS:1c3a, 1d04, 1cef, 1ce4). */
    const char *attacks, *backstabs, *slays, *behind, *cruel, *hitting, *point, *points, *damage;
    const char *misses, *lost_spell, *goes_down, *dying, *killed;
    /* Fleeing (432f:0dc7; dkk 5340:0cc0): away, or blocked. */
    const char *got_away, *blocked;
    /* Turning undead (432f:12b7; dkk 5340:119b): one destroyed, and none
     * turned (DS:2de3, 2df8). */
    const char *destroyed, *nothing;
    /* The computer's turn (3afb:004b, 1316; dkk 3f63:0000, 1260): one in
     * panic, and one that surrenders; Magic On's and Off's words, the
     * player's too (3afb:1200, 3995:0573; dkk DS:16f7, 1702). */
    const char *panic, *surrenders, *magic_on, *magic_off;
    /* The end of a round (3995:0b6d; dkk 3e05:0ed0): a member dying, and
     * the question once the enemy is gone; bandaging (6346:31e9; dkk
     * 65cd:3326, DS:2cf8). */
    const char *teammate_dying, *continue_battle, *bandaged;
    /* The panel (6346:0af6; dkk 65cd:0c44, DS:2c2e-2c4f): the hit points'
     * word, and one helpless or casting; guarding (6346:29b3; dkk DS:2cc2). */
    const char *hit_points, *helpless, *casting, *guarding;
    /* Alt-X without the Helm cheat (3995:0573), or NULL where it says
     * nothing (dkk 3e05:08af). */
    const char *gods_refused;
} cok_rules_fight_words;

/* The computer's choice of a spell to cast (ai.c), each the port's routine
 * of that game's original. */
typedef enum {
    /* 3afb:03b9 in 004b: 1d7 passes of up to three draws among the
     * memorized spells, the first that fits cast (cok_ai_cast). */
    COK_COMPUTER_CAST_CHAMPIONS,
    /* dkk 3f63:05d7: the memorized spell of the highest priority (DS:6a8d,
     * 16 bytes an id) that the computer may cast (6a8b), unless a d100 is
     * at most four times its mage, paladin, ranger and knight levels or a
     * d5 above its priority. */
    COK_COMPUTER_CAST_DEATH_KNIGHTS,
} cok_computer_cast_routine;

/* Turning undead (attack.c), each the port's routine of that game's
 * original. */
typedef enum {
    /* 432f:12b7: the cleric's levels (the former counted), 2 more for
     * deity 2; 1d12 undead, then the d20. */
    COK_TURNING_CHAMPIONS,
    /* dkk 5340:119b: the better of the cleric's level and the paladin's
     * less 2, 2 more for deity 2, above 8 made 9 (below 14) or 10, less
     * var COK_VAR_TURNING (0 at least); the d20 first, then at the first
     * undead found 1d6 + 6 of them where its row's value is negative (to
     * destroy), else 1d12. */
    COK_TURNING_DEATH_KNIGHTS,
} cok_turning_routine;

/* The rounds, the computer's turns and the attacks where the games differ
 * (round.c, ai.c, attack.c, arena.c, command.c, cloud.c), and what of them
 * the port does not carry out for a game yet (cited: reaching it stops the
 * run, COK_ECL_UNPORTED). */
typedef struct {
    cok_rules_fight_words words;
    /* The clouds (cloud.c, cok_clouds_routine) and their words: one in a
     * cloud that saves and one that does not (60f4:0dc3, CS:0d97 and
     * 0da7; dkk 638c:0c65, DS:1c06 and 1c1b), a cloud gone (3f44:0ff7,
     * CS:0fdc; dkk 638c:21ec, DS:1d4e), Stinking Cloud's (5b04:29de,
     * CS:29c6; none in dkk) and one Cloud Kill poisons (dkk DS:1ccf; none
     * in Champions). */
    uint8_t clouds;
    const char *cough, *choke, *clears, *noxious, *poisoned;
    /* The battlefield's cloud and pool tiles (6beb:0c9d: 0x1e, 0x1d; dkk
     * 6f03:0e33: 0x1d, 0x1c, Stinking Cloud's and Cloud Kill's) and
     * whether they also count as terrain and are
     * marked where a cell is off the map (dkk); whether a body is laid over
     * a pool's tile (6beb:0e08: only a cloud's is kept; dkk 6f03:0fbd's set
     * at CS:0f9d keeps both). */
    uint8_t cloud_tile, pool_tile;
    bool clouds_terrain, body_spares_pool;
    /* The death on the battlefield (6beb:0e08; dkk 6f03:0fbd): whether it
     * looks for the record among the bodies 1-8 to do nothing for one
     * already a body (dkk), rather than by a byte of the stack (see
     * arena.h); whether it clears the spell being cast (+0x00; dkk keeps
     * it). */
    bool kill_finds_body, kill_clears_spell;
    /* The computer's turn (3afb:004b; dkk 3f63:0000): whether one with no
     * target picks one first (dkk 5340:3cac with 0, 0, 1, 0xff, and before
     * a spell being cast with 0, 1, 1, 0xff); whether it uses an item's
     * spell (3afb:04b2; dkk 3f63:02b8 rolls its 1d7 and walks the items
     * but uses none); its choice of a spell (cok_computer_cast_routine);
     * the highest kind of undead it turns (3afb:024b reads a byte of the
     * stack: 0; dkk 3f63:0251 0x0d); the levels above which a paladin
     * turns undead too (0 none; dkk 2, and 5340:119b, 3e05:08af); its
     * reach, the weapon's range less reach_less (3afb:0d49: 1; dkk 0). */
    bool computer_target_first, computer_items;
    uint8_t computer_cast, turn_limit, paladin_turns, reach_less;
    /* Morale (3afb:1316; dkk 3f63:1260): whether 100 less var COK_VAR_MORALE
     * is compared signed (dkk, a byte) rather than as an unsigned word, and
     * whether one made to flee loses effects COK_EFFECT_DISPELLING and
     * GIANT_SLAYER (Champions). */
    bool morale_signed, flee_removes;
    /* The computer's step (3afb:095a; dkk 3f63:08c6): the turn added to the
     * way away from the party one fleeing takes (dkk 1), and whether the
     * way of moving is read only for a try below 6 and a way below 7 (dkk
     * 0ad7), rather than for every try. */
    uint8_t flee_turn;
    bool ways_guarded;
    /* Whether one may attack a target (432f:11d4; dkk 5340:10ab) runs the
     * target's event 1 with the attacker selected (DS:a2c2) and refuses a
     * target of its own side (dkk). */
    bool attack_sides;
    /* The free attacks on one stepping away (432f:0986; dkk 5340:0860):
     * an effect that denies them to one holding it, or 0 (dkk 6). */
    uint8_t free_attack_effect;
    /* The attack's message (dkk 5340:0230) and the damage's (dkk
     * 638c:1c52, cok_cast_damage): the bit of the creature flags' byte 1
     * (dkk +0x7b) that leaves out one's dying and killed lines, or 0. */
    uint8_t quiet_flag;
    /* A spell lost (6346:161b clears the memorized byte: 0; dkk 65cd:17c4
     * marks it, | 0x80). */
    uint8_t forget_mark;
    /* The missile's flight (432f:2beb; dkk 5340:28a0): the item types drawn
     * as arrows, 0-ended; whether its ends are first kept on the
     * battlefield (dkk 65cd:1d56: x 0-0x31, y 0-0x18). */
    const uint8_t *arrows;
    bool missile_clamped;
    /* The icon slot of the burst drawn on one hurt (6346:228c with kind 0:
     * 0x17; dkk 65cd:24bc 0x18; kind 1's flash is 0x16 in both). */
    uint8_t burst_slot;
    /* Turning undead (cok_turning_routine). */
    uint8_t turning;
} cok_rules_fight;
/* The clouds on the battlefield (cloud.c), each the port's routines of
 * that game's original. */
typedef enum {
    /* Stinking Cloud's clouds of four cells (5b04:29de), each its caster's
     * effect 0x28 while it lasts (3f44:0ff7); the acid pools (effect 0x46,
     * 3f44:2037, and 0x50, 2cad); one in a cloud or a pool (60f4:0dc3);
     * Dispel Magic's part (5b04:3352, 3151). */
    COK_CLOUDS_CHAMPIONS,
    /* dkk: Stinking Cloud's clouds of four cells and Cloud Kill's of nine
     * (5dc5:2934) in one list (DS:c443), each lasting rounds by its
     * caster level (638c:21ec, at the end of a round); one in a cloud
     * (638c:0c65); Dispel Magic's part (5dc5:2e5c, 2dd7). */
    COK_CLOUDS_DEATH_KNIGHTS,
} cok_clouds_routine;

/* The player's commands in a battle (command.c), each the port's routine
 * of that game's original. */
typedef enum {
    /* 3995:0573: the menu by letters (3995:08d5, "Move View Aim Use Cast
     * Turn Yell Done"), Done's (3995:121c: Guard, Delay, Quit, Bandage,
     * Auto, Speed), Move (3995:0d9c), aiming (432f:3b80), Speed (14cf). */
    COK_COMMANDS_CHAMPIONS,
    /* dkk 3e05:08af: the two-row menu by index (3e05:0ccc), its commands
     * Guard, Quick (Champions' Auto), Delay, Bandage, Speed and End (Quit)
     * on it, and no Done; Move (3e05:10bb), aiming (5340:3888 with 36f2,
     * 34b3, 2bee, 2f3d, 2dca) and Speed (3e05:14aa) with menus by index. */
    COK_COMMANDS_DEATH_KNIGHTS,
} cok_commands_routine;

/* The game's menu loop (menu.c): Champions' by the hotkeys' letters
 * (67b5:03e2, its Yes/No 67b5:177f), or Death Knights' by index (dkk
 * 6a69:0f1b: cok_menu_pick, its Yes/No 6a69:2048). */
typedef enum {
    COK_MENUS_LETTERS,
    COK_MENUS_INDEXED,
} cok_menus_routine;

/* The game's menus where they differ (menu.c; camp.c's Yes/No, the
 * adventure's menu, command.c). */
typedef struct {
    /* cok_menus_routine. */
    uint8_t routine;
    /* The Yes/No's items for the indexed loop (dkk 6a69:2048, DS:31cb). */
    const char *yes_no;
    /* The adventure's menu (475c:09ec; dkk 4307:0257): whether it keeps
     * its own selection, put back into DS:c0f7 before each ask and taken
     * after (dkk), rather than going on from what the menus before left
     * (Champions; both set the first before Cast, View and Encamp, whose
     * menus in Death Knights start from their first themselves); whether
     * Escape leaves Move (dkk: a special key in its loop). */
    bool adventure_keeps_selection, escape_leaves_move;
    /* Whether camp's menu keeps its own selection, from the first, put
     * back into DS:c0f7 before each ask and taken after (dkk 4381:3106's
     * [bp-4]), rather than going on from what the menus it ran left
     * (Champions' 4888:2c31, DS:6e0f). */
    bool camp_keeps_selection;
    /* Whether a special key that picks a character (546c:3334; dkk
     * 580d:2bb2) keeps the one selected for a key other than Up and Down
     * (dkk), rather than picking the first. */
    bool special_keeps;
} cok_rules_menus;

/* The player's commands in a battle where the games differ (command.c). */
typedef struct {
    /* cok_commands_routine. */
    uint8_t routine;
    /* The words of Move's steps and of attacking (3995:0d9c, 1117,
     * 432f:2f12; dkk 3e05:10bb, 13fb, 5340:2b81): fleeing's question, a
     * step too dear, a missile weapon in melee, an ally attacked
     * (Champions' in its code, dkk DS:170d, 1718, 2536, 16d7); and whether
     * Yes to attacking an ally turns the NPCs against the party (432f:2f12;
     * dkk 5340:2b81 sets only the variables). */
    const char *flee, *cant_go, *not_with_weapon, *attack_ally;
    bool ally_turns_npcs;
    /* COK_COMMANDS_DEATH_KNIGHTS' words, NULL for Champions': the turn's
     * menu (3e05:0ccc, DS:3453; '@' starts its second row), Move's prompt
     * and item (3e05:10bb, DS:313e, 34a4), aiming's prompt and items, the
     * range's word, the cursor's prompt and items (5340:2bee, 2f3d: DS:3109,
     * 34af, 16ec, 3114, 34d8), Speed's prompt and items (3e05:14aa, DS:30f4,
     * 340a). */
    const char *menu, *move_prompt, *move_menu;
    const char *aim_prompt, *aim_menu, *range, *cursor_prompt, *cursor_menu;
    const char *speed_prompt, *speed_menu;
    /* The icon slot of the cursor's box on the battlefield and in aiming's
     * flight (6beb:00fc, 6346:1a26: 0x19, COMSPR 25; dkk 6f03:036c,
     * 65cd:1bd5: 0x1c, COMSPR 15). */
    uint8_t cursor_slot;
} cok_rules_commands;

/* View and the items menu (sheet.c, items.c): the routine of each, as
 * the game's original has them. */
typedef enum {
    /* 546c:0d74 and 546c:17f9: menus by letters (67b5:03e2), the items
     * listed by 67b5:1368. */
    COK_ITEMS_CHAMPIONS,
    /* dkk 580d:0c66 and 580d:0ffa: menus by index (6a69:0f1b), the items
     * listed by 6a69:1b82 (cok_menu_indexed_rows), View's Deposit in mode 7
     * and its Heal and Cure, the items menu's Deposit in mode 7. */
    COK_ITEMS_DEATH_KNIGHTS,
} cok_items_routine;

/* View and the items menu where the games differ (sheet.c, items.c; see
 * View): the routines, their words and rules. */
typedef struct {
    /* cok_items_routine. */
    uint8_t routine;
    /* COK_ITEMS_DEATH_KNIGHTS' menus, every item each may offer, by index
     * (dkk View's DS:3276, the items menu's DS:32c7); NULL for Champions,
     * whose menus join their words in the code. */
    const char *view_menu, *items_menu;
    /* The readied column of the items list (6346:0488's 0473, 047a; dkk
     * 65cd:047f's DS:2c02, 2c0d, a space shorter), each item's name built
     * after it. */
    const char *readied_yes, *readied_no;
    /* Ready (546c:1ea7; dkk 580d:1717): the thief level (+0xff's thief,
     * dkk +0x4b) at and above which a thief readies a scroll (type 0x27)
     * of either order of magic, and the word for a thief of a level below
     * it but above 0 ("Thief Level Too Low", dkk 580d:1703; the others
     * "Wrong Magical Order"); 0 and NULL for no such rule (Champions). */
    uint8_t thief_scrolls;
    const char *thief_too_low;
    /* Use of an item not readied (546c:17c4; dkk DS:111f). */
    const char *must_ready;
    /* The questions of parting with a scroll marked to scribe (546c:10b3,
     * "is it Okay to lose it? "; dkk 580d:0efb's DS:0f1a) and of Drop
     * (546c:17ef, "Drop It? "; dkk 580d:0ffa's DS:0f1a), both from No. */
    const char *lose_scroll, *drop_question;
    /* Trade's pick of a partner for money (546c:2c31, "Trade to?"; dkk
     * 580d:23a0's DS:1061), and the coin lists' first item of Trade and
     * Drop (546c:2c52, 2fc0, " Select"; dkk 580d:23a0, 26d0's DS:31d6). */
    const char *trade_to, *coin_select;
    /* COK_ITEMS_DEATH_KNIGHTS' Deposit of an item in the vault (580d:0ffa
     * in mode 7): the words before and after its name (580d:0fda, 0fe0). */
    const char *deposit_before, *deposit_after;
    /* Sell (546c:2822; dkk 580d:2077), in shops: the offer's words before
     * the price and before the name, the question, "Sold!" and the money
     * that would overload the character (Champions' 546c:27be, 27cd, 27e5,
     * 27f4, 27fa; dkk DS:1134, 1149, 1168, 117d, 1188); what a price
     * times a count above 1 is divided by (Champions' 20; dkk 1); and
     * whether Sell's and Id's question starts on Yes (dkk 6a69:2048 with
     * DS:57c0), rather than on No. */
    const char *offer, *offer_for, *deal, *sold, *sold_pool;
    uint8_t sell_divisor;
    bool deal_yes;
    /* Id (546c:2a59; dkk 580d:21d3), in shops: the offer's, the
     * nothing-new's and the shown's words before the name, and too little
     * money's (Champions' 546c:29ce, 2a17, 2a3d, 2a06; dkk DS:11b1,
     * 11da, 1203, 1743); and whether a name shown waits a second time
     * (dkk). Its 100 steel are paid as the shops' prices are (the shops
     * section's steel). */
    const char *identify, *nothing_new, *looks_like, *no_money;
    bool id_waits_twice;
    /* COK_ITEMS_DEATH_KNIGHTS' View's Heal and Cure (580d:2e95, 2f47,
     * 2ee0, 3018), offered outside combat to one of the class (+0x21) that
     * is okay (+0xc7): Heal while it has no effect heal_effect, healing
     * twice its level in that class (+0x45 + class) of one picked from the
     * party's first and giving it the effect for heal_minutes; Cure while
     * +0x25 is not 0, picking one and doing nothing more. The prompts, and
     * the words after the name healed or not (580d:2f1e, 300c, 2f2a,
     * 2f38). */
    uint8_t heal_class, heal_effect;
    uint16_t heal_minutes;
    const char *heal_whom, *cure_whom, *feels_better, *unaffected;
} cok_rules_items;

/* The shop, its Buy and Appraise, and the temple and its Heal, as each
 * game has them (shop.c): Champions' (36d0:07da, 0484, 58e7:1929, 340d:0ea9,
 * 0be1), by letters, prices paid in every coin and the cures for steel;
 * or Death Knights' (dkk 3b89:06f3, 0453, 5c72:0dc2, 3999:07d3, 04e2), by
 * index, prices paid in steel and the cures free, some by their spells'
 * handlers. */
typedef enum {
    COK_SHOPS_CHAMPIONS,
    COK_SHOPS_DEATH_KNIGHTS,
} cok_shops_routine;

/* Shops and the temple where the games differ (shop.c, items.c's Sell and
 * Id; see Shops and the temple, and Death Knights' shops). */
typedef struct {
    /* cok_shops_routine. */
    uint8_t routine;
    /* Mode 1's redraw (6346:2c17; dkk 65cd:2db0, which mode 7 shares):
     * whether in 3D areas it shows the portrait last shown in place of the
     * small picture's first frame (Champions), rather than the picture
     * always (dkk). */
    bool portrait;
    /* Whether a price (Buy's, Id's) is paid from the selected character's
     * steel (+0x3f) and what that lacks from the pool's steel (DS:be58, a
     * long: dkk 3b89:0453, 580d:21d3), rather than from its money in steel
     * of every coin (546c:3424), its coins then all steel (58e7:0155), or
     * else the pool's worth (58e7:00d3, 018f; Champions). */
    bool steel;
    /* COK_SHOPS_DEATH_KNIGHTS' words, NULL for Champions, whose shop.c
     * holds its own: the shop's and the temple's menus, every item each
     * may offer, by index (DS:34f8, 3594); Buy's list's menu (DS:34ed;
     * its prompt the treasure section's items_prompt, DS:315d) and the
     * funds shown under it (3b89:0000's DS:2aa2, 2ab7), the words of a
     * purchase (DS:1738 for an item without a count, 172d with one; too
     * little money is the items section's no_money, DS:1743); and the
     * words of leaving money in the pool (DS:1758, 17a9, the temple's
     * too). */
    const char *menu, *temple_menu, *buy_menu, *personal_funds, *pooled_funds;
    const char *buys_a, *buys, *left, *go_back;
    /* COK_SHOPS_DEATH_KNIGHTS' Appraise (5c72:0dc2): none to value
     * (DS:1081), the counts' words (0fd9, 0fe4), the collection's line
     * (0fef), the menu (3223) and its prompt (3003), the value's words
     * (100e, 102d; 1096 after the value) and the menu after it (3238);
     * what would overload is the items section's sold_pool (5c72:00ba,
     * DS:1188). */
    const char *no_gems, *gems, *jewelry, *collection, *appraise_menu, *appraise_prompt;
    const char *gem_valued, *jewel_valued, *valued_end, *sell_keep;
    /* COK_SHOPS_DEATH_KNIGHTS' Heal (3999:04e2): its cures, the spells
     * whose names its list shows (DS:030c, cure_count of them, each named
     * from the spell names), its menu (DS:3589) and the words after the
     * name (DS:1aa6, a string[20] that cuts them); the question of a cure
     * not needed (3999:0000, DS:19c9) and the words of each (3999:0053
     * "is not blind.", 01fc's two, 02a0, 0368, 0404, 045f: DS:19fe, 1a28,
     * 1a3d, 1a52, 1a67, 1a7c, 1a91). Raise Dead, Remove Curse and
     * Restoration run their spells' handlers (3999:01fc's 0x4b, 0368's
     * 5dc5:311b, 045f's 0x11: cures 7, 8 and 10). */
    const uint8_t *cures;
    uint8_t cure_count;
    const char *heal_menu, *help, *anyway, *not_blind, *cannot_raise, *not_dead;
    const char *not_poisoned, *not_cursed, *not_stoned, *no_lost_levels;
} cok_rules_shops;

/* Treasure where the games differ (treasure.c, adventure.c's DAMAGE and
 * ADD EP): the item table that TREASURE ITEMS and STOCK take their items
 * from, the treasure menus' words and ways, the vault's menu, and DAMAGE's
 * and ADD EP's rules. */
typedef struct {
    /* The item table (dkk 4f9a:0006, the end of startup): table's record
     * (ITEM0 record 1) read 17 bytes an entry, an item's +0x2e-+0x3e, into
     * entries 1 to at most table_count (0x7f) of DS:a2ca (entry id at
     * DS:a2b9 + 17 * id), the rest of its 254 entries 0xff (an entry
     * whose first byte is 0xff is none); a stem of NULL for none
     * (Champions, whose TREASURE reads ITEM<file> records whole). */
    cok_rules_record table;
    uint8_t table_count;
    /* The treasure menu (351b:118c; dkk 3a37:0eed), Take (351b:0ff7; dkk
     * 3a37:0dfc), its items (351b:0ec5; dkk 3a37:0ce8) and money (58e7:0d01;
     * dkk 5c72:0ac4): whether each menu's selection starts at its first
     * item (dkk: DS:c0f7 set to 1, the treasure menu's kept apart from its
     * submenus'), rather than going on from the menu before (Champions'
     * DS:6e0f); whether the menus tell special keys from letters (dkk
     * DS:c0f8: Escape leaves, up and down pick a character, the others do
     * nothing; Champions' take a special key's scan code as a letter, so
     * PgUp is Take's I); the words of leaving with treasure left and of
     * going back (Champions' 351b:1133, 113c; dkk DS:194f, 1978), and
     * whether the Yes/No after them starts on No (dkk 6a69:2048 with 1;
     * Champions' keeps the menu's selection); Take's prompt, the items'
     * and the coins' (Champions' "Take: ", "Items: ", "Select type of coin
     * "; dkk DS:3168, 315d, 104c), and the coin questions' words: their
     * first for gems and for the others (58e7:0b95; dkk DS:0fae, 0fce) and
     * each coin's word (dkk 5c72:09e4, DS:0c62 + a space), by coin. */
    bool menus_first, specials_apart;
    const char *left, *claim;
    bool claim_no;
    const char *take_prompt, *items_prompt, *coin_prompt;
    const char *how_many, *how_much;
    const char *coin_words[7];
    /* Taking an item (36d0:034c; dkk 3b89:033b): its word when overloaded
     * (Champions' CS:0341 "OverLoaded"; dkk DS:2acc), and whether an item
     * with a count joins those of its kind (dkk 580d:1be6, as View's Join)
     * once taken, one it would join not counting against the 16 items
     * (dkk 580d:2a0a). */
    const char *overloaded;
    bool take_joins;
    /* The vault's menu (dkk 3948:000e, COMBAT's vault branch; the combat
     * section's vault): its items by index (dkk DS:35bd: View Take Pool
     * Money Items Exit), the line mode 7's redraw shows (dkk 65cd:2db0,
     * DS:2ccd, at 1, 15 in light green), the deposit's question's end
     * (dkk 580d:26d0 in mode 7, DS:1237, also View's money in mode 7);
     * vault_items NULL: the game has none (Champions). */
    const char *const *vault_items;
    const char *vault_line, *deposit;
    /* DAMAGE (2fd3:2c80; dkk 3632:1ca2): the first operand's bits that are
     * a saving throw's bonus (Champions' 0x1f; dkk 0x0f); whether its bit
     * 0x10 halves the damage when the save is made (dkk), rather than
     * dealing it whole; and the key after it (Champions' 2fd3:2c5d "press
     * <enter>/<return> to continue"; dkk DS:0868). */
    uint8_t damage_bonus;
    bool damage_halves;
    const char *damage_key;
    /* ADD EP (2fd3:36dc; dkk 3632:25f1): the words for one character,
     * before and after its name (Champions' 2fd3:36b8, 36c9; dkk DS:1485,
     * 2a64), and for the party (DS:2a3b, a string[40]); whether a
     * character that cannot act is passed over before its classes divide
     * the points (dkk), rather than after; and whether it sets whether one
     * may train, as the end of combat does (dkk 65cd:38c7, +0xc6; the
     * combat section's may_train). */
    const char *gains, *gains_end, *party_gains;
    bool gains_active_first;
} cok_rules_treasure;

/* A race's age past which a battle that aged a character says it is very
 * old, and past which it dies (dkk 3e05:0264), as words. */
typedef struct {
    uint16_t old, dies;
} cok_rules_age;

/* Training, the knights' orders, aging, Memorize and Scribe where the
 * games differ (train.c, magic.c, camp.c; see Training and Camp). */
typedef struct {
    /* Train Character (4def:4d9e; dkk 49cb:43b6): its words (one who
     * cannot act, a hall that trains none of the classes, too little
     * experience; " will become:", the first and the other lines' starts,
     * the question and the answer to Yes: Champions' 4def code, dkk
     * DS:1542, 1561, 1412, 1427, 143c, 1451, 1466, 1485); whether the
     * question's Yes/No starts on Yes (dkk 6a69:2048 with DS:57c0's 0)
     * rather than on No (67b5:177f); whether free training (DS:7140) trains
     * (dkk 49cb:43b6 clears DS:c46a first: never); whether the highest
     * levels by race and class (COK_DATA_LEVEL_LIMITS, dkk DS:82d4) stop
     * a class, rather than the kender's fighter and ranger limits by
     * strength; the experience row the original's uninitialized local
     * gives the first class without one (Champions' 0x74, see train.c),
     * 0 where the port does not know it (a stop); whether a ranger above
     * level 8 learns a spell only when it rises (dkk), rather than every
     * time it trains. */
    const char *conscious, *wrong_hall, *no_experience, *will_become, *a_level, *and_level;
    const char *question, *congratulations;
    bool question_yes, free_training, race_limits;
    uint8_t stale_row;
    bool ranger_rises;
    /* The hit points a level brings (4def:4b3a; dkk 580d:3193): by class,
     * the fixed amount a class at or above its top level (DS:3903) gives
     * in place of the sum so far, 0 none (Champions' fighters and knights
     * 3, clerics, rangers and thieves 2, mages 1; dkk paladins 3 too); and
     * whether the hit points stay as far below the new maximum as they
     * were only down to 0 (dkk), rather than wrapping as a byte. */
    uint8_t top_hit_points[8];
    bool hit_points_floor;
    /* Knight Change (4def:5812; dkk 49cb:4ea4): "Knight Of The " before the
     * order (dkk DS:14cf), the orders granted on row 11 (Champions'
     * "Sword", "Rose"; dkk the petition's lower-case DS:14b9, 14c4),
     * "NAME is too inexperienced" by order (dkk DS:14e4 for both), and
     * what a knight of no order petitions for, a local never set
     * (Champions' empty, see train.c; NULL where the port does not know:
     * a stop); whether
     * Change is offered whatever the petition's outcome (dkk: DS:3361
     * always) and the menu's special keys do nothing but Escape (dkk
     * DS:c0f8; Champions' take F9's scan code as C); the knight level from
     * which a lost level costs 2 hit points and no constitution
     * (dkk 49cb:4da1: 10), 0 for none. */
    const char *knight_of, *granted[2], *inexperienced[2], *no_order;
    bool change_always, specials_apart;
    uint8_t promote_fixed;
    /* The aging after a battle that aged someone (dkk 3e05:0264, effect
     * 0x27 setting DS:c463, game->effects.aging), or none (ages NULL):
     * by race (+0x20), ages (+0x26), age_races of them; the words "NAME
     * who is looking very old", "the strain of the last battle has worn
     * heavily on NAME.  " (DS:2319, 2338, 3e05:0260), "he"/"she" and
     * "breathes deeply ..." (DS:23da, 23e5, 2389), dying's "dies from age"
     * (DS:23f0). */
    const cok_rules_age *ages;
    uint8_t age_races;
    const char *very_old, *strain, *strain_end, *he, *she, *last_breath, *died;
    /* The spell lists (5b04:027a; dkk 5dc5:016e): the prompt of a list to
     * pick from ("Choose Spell: "; dkk DS:2fd9); the memorize table
     * (4888:0b24; dkk 4381:09c7, as many levels as the record's spells a
     * day): its labels at column 1, its first level's column (each next 3
     * on), the bonus spells' label and column. */
    const char *choose_prompt;
    const char *table_labels[4], *bonus_label;
    uint8_t table_column, bonus_column;
    /* Whether the table first says the character's name in the text
     * window (4888:0b24's 6346:1883 with "", clearing rows 17-22), rather
     * than clearing rows 18-21 alone (dkk 4381:09c7's 110f:0584). */
    bool table_name;
    /* The spells left of a power granted to one without a cleric level
     * (4888:0700's uninitialized local, which Memorize's stack leaves
     * 0x71, see Camp; dkk 4381:0664's own, not known: 0, a stop). */
    uint8_t granted_stale;
    /* Memorize's and Scribe's spells already marked (4888:1098, 132b; dkk
     * 4381:0f67, 115d): Memorize's list asks to keep them with a word of
     * its own (dkk mode 5: DS:30ab "spells OK? " and Keep, Escape
     * unmarking), or where keep is NULL with the question, before and
     * after; Scribe's question, before and after; the questions' colour
     * and whether they start on Yes (dkk 6a69:2048 with DS:57c0); the
     * words of Scribe (a spell known, one being scribed, one that cannot
     * be: dkk DS:2f46, 2f65, 2f8e); and whether the camp's start and end
     * unmark the spells to memorize as well as the scrolls (4888:06b8;
     * dkk 4381:0626 the scrolls only). */
    const char *keep, *keep_prompt;
    const char *memorize_question[2], *scribe_question[2];
    uint8_t question_colour;
    bool questions_yes;
    const char *known, *scribing, *cannot_scribe;
    bool camp_forgets_memorized;
    /* The colour of Display's names (4888:17ae passes 11; dkk 4381:15aa's
     * list draws its headings in DS:c149's 13), and whether its first
     * blank row and the blank after each member are headings too (dkk
     * 4381:13fb with 1), so that the list starts on its last row. */
    uint8_t display_heading;
    bool display_blank_headings;
    /* Whether the Magic menu keeps a selection of its own, from its first
     * item each time it opens, across what its commands show (dkk
     * 4381:1a34 with DS:c0f7), rather than going on from the menu before
     * (4888:1c32). */
    bool magic_selection;
    /* The camp's Fix where the port does not carry the game's out yet
     * (dkk 4381:2fd4, with its cures 2a73), citing it, or NULL: choosing
     * it stops the run (COK_ECL_UNPORTED). */
    const char *fix_unported;
} cok_rules_levels;

/* A spell whose cast bursts on its cell after the flight in combat (dkk
 * 5dc5:1298's calls of 65cd:39bd): the icon slot of its pictures, and its
 * rings (3 * n + 1), n where var COK_VAR_3D is 0 and n_3d where not. */
typedef struct {
    uint8_t spell, slot, n, n_3d;
} cok_rules_burst;

/* The computer's pick of a spell's target (432f:210b; dkk 5340:1eeb). */
typedef enum {
    /* 432f:210b: itself for a spell not hostile (Cure Light Wounds the
     * wounded ally beside it), else one within range 432f:3f9f picks,
     * unless helpless and the spell's effect one of those that make it so. */
    COK_CAST_PICK_CHAMPIONS,
    /* dkk 5340:1eeb: one within range of the side the spell is for
     * (5340:1da5: 5340:3cac with its allies flag, then 5340:3fd5's worth),
     * or for an area spell but a line (column 6 of 9 and up) the cell
     * whose area does the most good (5340:44f8). */
    COK_CAST_PICK_DEATH_KNIGHTS,
} cok_cast_pick_routine;

/* Casting where the games differ (cast.c, effect.c's magic resistance,
 * magic.c's Cast, party.c's load): the cast's words and ways (5b04:1415;
 * dkk 5dc5:1298), its targets (5b04:127e, 432f:2337, 210b; dkk 5dc5:1182,
 * 5340:1ff7, 1eeb), the damage's message (60f4:1db7; dkk 638c:1c52). */
typedef struct {
    /* Outside combat a spell cast only in combat: its name, then "can't be
     * cast here..." and "Lose it? " (dkk DS:0f25, 0f1a); an item's "That
     * Item", "is a combat-only item..." and "Use it? " (DS:0f44, 0f4f,
     * 0f6e); in combat one picking no target, "Abort Spell? " and "Spell
     * Aborted" (DS:0f84, 0f99); and whether these questions start on Yes
     * (dkk 6a69:2048 with DS:57c0, colour 13), rather than on No in the
     * colours 13 and 14 (67b5:177f). */
    const char *cast_here, *lose_it, *that_item, *combat_only, *use_it;
    const char *abort, *aborted;
    bool ask_yes;
    /* The pick outside combat (5b04:127e; dkk DS:3192, the pick by index
     * 65cd:33c9 with "Select" alone, see the menus section), the line said
     * in combat on casting (5b04:57eb; dkk DS:2304). */
    const char *on_whom, *casts_a_spell;
    /* Whether one holding COK_EFFECT_DISPELLING miscasts on a 1d2 of 1
     * (5b04:1415's 6346:2447 with 0x4a; dkk none). */
    bool miscast;
    /* The spells that burst after their flight (dkk 65cd:39bd), and the
     * sound the burst plays (DS:4316, Fireball's). */
    const cok_rules_burst *bursts;
    unsigned burst_count;
    uint8_t burst_sound;
    /* The targets in combat (432f:2337; dkk 5340:1ff7): whether the list is
     * cleared first (dkk 1a07:1ad4 of 0x120 bytes at DS:c31d, entries
     * 1-72); whether Silence's (column 6's 0x0f) area around its cell has
     * column 6's high nibble for its radius (dkk), rather than that of the
     * low nibble shifted, 0 (Champions). */
    bool targets_cleared, silence_radius;
    /* The computer's pick of a target (cok_cast_pick_routine). */
    uint8_t computer_pick;
    /* 5b04:1e34 (dkk 5dc5:1cc6): whether every spell keeps out an ally
     * with an enemy beside it in combat, not Bless's alone. */
    bool sided_all;
    /* 5b04:1cbd (dkk 5dc5:1b20): whether Charm Monsters' effect is valued
     * at the caster level with the caster's side (+0xc9) in bit 7 (dkk),
     * rather than at the caster level alone. */
    bool missiles_charm_side;
    /* 5b04:3a6f (dkk 5dc5:354a), a bolt: whether its first bounce doubles
     * the length walked rather than adding 8, and the least it is charged
     * a line (dkk 2, a signed word), 0 for Champions' byte arithmetic. */
    bool bolt_doubles;
    uint8_t bolt_least;
    /* Magic resistance (60f4:04f3; dkk 638c:04c9): whether magic damage
     * with no effect pending is resisted too (dkk). */
    bool resist_damage;
    /* The damage's message (60f4:1db7; dkk 638c:1c52, DS:1c32-1d04):
     * "takes ", N, " points of damage " or "takes 1 point of damage ",
     * then "from Fire", "from Cold", "from Electricity", "from Acid", "from
     * Magic"; one hurt while casting "lost a spell"; one down "Goes Down",
     * ", and is Dying" or "is killed", but one whose creature flags hold
     * the fight section's quiet bit only goes down (dkk). */
    const char *takes, *points, *point, *from[5], *lost_spell, *goes_down, *dying, *killed;
    /* Cure's word (60f4:14b7; dkk DS:19f3). */
    const char *cured;
    /* Why one may not cast, memorize or scribe (4888:08d8; dkk 4381:07ff,
     * DS:2e0d-2e6b, cut to their strings' lengths): the area forbids it;
     * "is in no condition to " and what. */
    const char *no_area, *no_condition, *no_what[3];
    /* A body's cell in combat (6beb:0500 draws its tile, RANDCOM's; dkk
     * 6f03:010d the cell under it, then the icon of slot body_icon past the
     * body's tile, masked), or 0 for none. */
    uint8_t body_icon;
    /* Whether a character's load (4b6d:11e5; dkk 46b2:10d7)
     * recomputes its strength between its stats and its levels (dkk
     * 638c:16d8 with 0). */
    bool load_strength;
    /* A cone (5b04:1903; dkk 5dc5:175d): whether the sight's range is the
     * cone's length (dkk sets its local [bp-0x78] from the argument),
     * rather than what the caster's flight left on the stack (Champions':
     * no limit, see cast.c's fear); and whether the rest of what that
     * flight left (the side where no one is on the line, the directions
     * aimed at the caster's own cell) is not known to the port (dkk: the
     * side is tried at each of its values, the own cell stops the run). */
    bool cone_range_length, cone_stack_unknown;
    /* Why a cone with no one on its line has side lines the port does not
     * know, where its targets differ by them: the game's own reason (dkk
     * 5dc5:175d's [bp-0x76], set only on a cell with an occupant), or NULL
     * for Champions' (a breath's: the CGA mirror's mask, 127f:0b94,
     * 5b04:1b2a). */
    const char *cone_side_unknown;
    /* Fire Shield's question for a COK_CAST_FIRE_SHIELD entry of value 1
     * (dkk DS:2fee), or NULL. */
    const char *flame_type;
} cok_rules_casting;

/* The game's own words that are names: its title. */
typedef struct {
    /* The window's title. */
    const char *name;
    /* The title's menu's heading, at startup and after a demonstration
     * (1000:0134), which spell it differently. */
    const char *version, *version_again;
    /* Camp's first line (4888:2c31; dkk 4381:3106, DS:16b7). */
    const char *camp;
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
    cok_rules_start start;
    cok_rules_combat combat;
    cok_rules_fight fight;
    cok_rules_menus menus;
    cok_rules_commands commands;
    cok_rules_items items;
    cok_rules_shops shops;
    cok_rules_treasure treasure;
    cok_rules_levels levels;
    cok_rules_casting casting;
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
