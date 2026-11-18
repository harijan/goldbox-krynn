#ifndef COK_RULES_H
#define COK_RULES_H

#include "data.h"
#include "field.h"
#include "spells.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct cok_adventure cok_adventure;

/* Which routine says whether a load is asked (cok_adventure_load_asked), or none. */
typedef enum { COK_LOAD_ASKED_NONE, COK_LOAD_ASKED_DARK_QUEEN } cok_load_asked;
/* COMBAT with no monsters' treasure branch (rules.h's combat no_monsters_treasure). */
typedef enum { COK_NO_MONSTERS_TREASURE_NONE, COK_NO_MONSTERS_TREASURE_DARK_QUEEN } cok_no_monsters_treasure;

/* What differs between the games the port plays: one descriptor a game,
 * chosen once at startup (--game) and reached through cok_game_rules (and,
 * in the tools without a game, passed in). Shared code reads what it needs
 * here and never asks which game it is. Champions of Krynn's is
 * cok_rules_champions, Death Knights of Krynn's cok_rules_death_knights,
 * The Dark Queen of Krynn's cok_rules_dark_queen (read headless so far);
 * see docs/games.md's "Games".
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

/* How a game's archives hold their records: DAX archives (dax.h), a byte
 * id a record, compressed, a file for each ECL file (<stem><file>.DAX);
 * or Dark Queen's HLIB libraries (hlib.h), uncompressed, one file for
 * every ECL file, the stem its whole name (extension too: "GEO.GLB"). */
typedef enum { COK_ARCHIVE_DAX, COK_ARCHIVE_HLIB } cok_archive_format;

/* The archives the game reads, as stems (see above). */
typedef struct {
    cok_archive_format format;
    /* The folders under the assets' that its files are looked for in after
     * the assets' own, NULL-ended (Dark Queen's DISK1-DISK3, where its
     * installed game keeps them: it reads disk1\\dqk.cfg, DS:608e); NULL
     * for none. */
    const char *const *folders;
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
    /* Or one library whose records by id hold each monster whole (Dark
     * Queen's MONCHA.GLB, dqk 604b:257c: a big-endian size, then PackBits
     * of the record, a count of effects and of items, its effects of 5
     * bytes and its items of the item record's file_size); NULL where the
     * kinds above are files of their own. */
    const char *monster_library;
    /* The battlefield's tiles: in a 3D area, outdoors, and the random
     * ones of both (3cb2:1029). */
    const char *dungeon, *wilderness, *random;
    /* The title's music: the drivers (1661's, record device + 1) and the
     * score (record 1); or, for a game with songs, their file (dkk
     * MUSIC.DAX: record 0 the driver, 15dc:00d3, record n song n,
     * 15dc:019a; see songs.h), the others NULL. */
    const char *music_drivers, *music_score, *music_songs;
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
    /* The paths after its third line (11c7:031a: 1, the save path DS:5784;
     * dkk 1187:011b: 2, its save path DS:98a4 and Champions' saved games'
     * DS:99a4), before the F line and the stick's thresholds. */
    uint8_t config_paths;
    /* A devices' file of bytes rather than lines: its size, which a read
     * of it must give (dqk 6478:0000 reads DQK.CFG's 0x90), and the
     * folder of the assets it is in (DISK1); 0 and NULL for the text
     * files. */
    uint16_t config_bytes;
    const char *config_folder;
    /* The item types (3e99:005b), a file of its own, not an archive, and
     * the bytes before them in it (Champions' 2, the loader seeking past
     * them; dqk 64b2:0006 reads ITEMS.DAT from its start). */
    const char *item_types;
    uint8_t item_types_at;
    /* The item table in a file of its own (dqk 64b2:066c: ITEM.DAT's
     * 17-byte entries into ids 1 on, DS:c6de's 0x10ef bytes cleared to
     * 0xff first); NULL where the game has none or keeps it in an archive
     * (treasure.table). */
    const char *item_table;
    /* The directory the original runs in (FExpand('*.exe') split,
     * DS:4a80: the current directory), which the setup offers the save
     * path under: the port has no DOS directory and takes the one the
     * assets' own config says the game was installed in (its save path
     * C:\COK\SAVE\). And the save path's default under it, DS:4a25
     * (11c7:0388). */
    const char *install_dir, *save_dir;
} cok_rules_files;

/* The setup that writes the devices' file when there is none (11c7:031a;
 * dkk 1187:011b), on the text screen before the graphics mode: its words,
 * its answers and the letters it writes, and what startup then reads of
 * the file (setup.c, config.c). */
typedef struct {
    /* The first screen, Write without a line end, a digit 1-3 for C, E or
     * T (11c7:0114; dkk DS:2744). */
    const char *display;
    /* Whether the display's [3] Tandy makes the input's [2] mouse write N
     * (dkk DS:8b8f, set at 1187:02e2, read at 0449); false in Champions. */
    bool tandy_no_mouse;
    /* The sound screen's lines, each WriteLn, NULL-ended (11c7:014a-01c4;
     * dkk DS:2795-2811, five), the highest digit it takes (6 in both),
     * and the letter each answer from 1 writes as the second line: an
     * answer past the letters writes none (dkk's 6, which none of its
     * lines offers: 1187:03db's cases end at 5). */
    const char *const *sound;
    uint8_t sound_answers;
    const char *sound_letters;
    /* The letters the file's second line reads as the devices 0-4 (CMS,
     * Roland, AdLib, Tandy, the speaker), 0 where no letter does; any
     * other is no sound (11c7:0a5a's CRATP; dkk 1187:0a2c's C, A, T, P:
     * no Roland). */
    char sound_devices[5];
    /* The input screen, Write without a line end, a digit 1-3 for J, M or
     * N (11c7:01dc; dkk DS:2830). */
    const char *input;
    /* Whether the input's flags (dkk DS:dbf2, the joystick, and dbaa, the
     * mouse) lie past the image and are never set when the file's third
     * line is none of J, M and N, which the game's start then reads (dkk
     * 4f9a:0123): the run stops there. False in Champions, whose flags
     * (DS:89ce, 8986) startup leaves off. */
    bool input_unset;
    /* The paths after the first (files.config_paths), each asked as dkk
     * 1187:0687 asks Champions' saved games' path (DS:99a4): a line
     * (WriteLn; DS:28e0), then the prompt's start (1187:00fa) before its
     * default and "):  "; the default is the save path's drive letter
     * (11c7:0388's base) and, below C, floppy (DS:28c0), else drive
     * (DS:28cb). NULL in Champions, which has none. */
    const char *other_title, *other_prompt, *other_floppy, *other_drive;
    /* The joystick's calibration on the text screen (177c:029d; dkk
     * 18bc:01e1), each after ClrScr and WriteLn: the upper left and lower
     * right corners (77c:01d7, 0253; dkk DS:1b2e, 18bc:01df's ' ' and
     * DS:1b57, and DS:1b9f, 1bc8), then for every failure its line and
     * Delay(2000) (77c:0220; dkk DS:1b80). */
    const char *calibration[2], *calibration_failed;
    /* The note shown at every start, after the setup or the file, and
     * Delay(2000) (11c7:0299; dkk DS:2931). */
    const char *numlock;
} cok_rules_setup;

/* How a character's files hold it (party.c's cok_character_read_file and
 * write_file). */
typedef enum {
    /* The record, and its items and effects (and spells' block) in files
     * of their own (4b6d:11e5; dkk 46b2:10d7). */
    COK_CHARACTER_FILES,
    /* One file (Dark Queen's .QCH: dqk 604b:05fd reads, 604b:2200
     * writes): the record, its items' file bytes (the item record's), a
     * container's contents after it, then its effects of 9 bytes, each
     * with its next pointer. */
    COK_CHARACTER_ONE_FILE,
} cok_character_files;

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
    /* How the files hold a character (one file: items, effects and spells
     * NULL). */
    cok_character_files character_files;
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
    /* A saved game that holds its characters whole (cok_saved_game's
     * one_file: dqk 604b:28f8 reads it, 604b:31a7 writes it), of
     * game_size bytes at least: 0 for the layout above. */
    uint16_t game_size;
    /* A vault of the item record's items, padded to 50 of them
     * (cok_vault_read_game: dqk 63ee:0239, 00df), or 0 for Death Knights'
     * (cok_vault_read). */
    uint8_t vault_items;
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
     * from the handler's; for a list opcode, skip_list: the whole list, as
     * its handler loads it (dqk 6c70:1385), rather than skip operands. */
    uint8_t skip;
    bool skip_list;
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
    /* Where the variables lie (3775:0718's stores; dqk 6c70:0377), each a
     * first address and a size, 0 for none: the area's (vm->mem4b00,
     * 0x4b00-0x4eff; dqk 0x0001-0x0400, the 1 KB at far DS:b20f), the
     * game's (mem7c00, 0x7c00-0x7fff, beneath the selected character's
     * fields) and the scratch (mem7a00, 0x7a00-0x7bff); the block's
     * buffer from 0x8000 (DS:4b34's 0x1e00, dqk DS:ae5c's 0x2ee3: 64b2:0131
     * clears it) and the bytes a block's record holds before its code
     * (DAX's two, 88 13; none in Dark Queen's ECL.GLB, 6c70:0118); and the
     * address whose string is the selected character's name (0x7c00; dqk
     * 0x401, 6c70:0c18). Another address reads 0 and a store to it does
     * nothing, but for the special ones below. */
    uint16_t area_first, area_size, game_first, game_size, scratch_first, scratch_size;
    uint16_t code_size;
    uint8_t block_header;
    uint16_t name_address;
    /* Whether a store to a special address writes the variable there too
     * (dqk 6c70:0817 after 6c70:0728's), rather than only the state. */
    bool special_stores_kept;
    /* Whether the selected character's addresses (game_first, game_size)
     * are its fields alone (dqk 6c70:040d, 1b5e): one no field is reads 0
     * and a store to it does nothing, rather than a variable's. */
    bool game_fields_only;
    /* Whether COMPARE AND finds a pair whose first is a string unequal
     * always (dqk 5f07:05be compares the slots' addresses, not their
     * strings, testing the first's type alone). */
    bool compare_and_unequal_strings;
    /* How many of the flags COMPARE AND and a block's start clear, from flags[0] (dqk 5f07:05c4
     * and 6c70:003b clear 4; Champions' and Death Knights' 6, as their fillchars do). */
    uint8_t flags_cleared;
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
    /* And addresses no code names otherwise that it sets, count of them
     * (dqk 6c70:0001: 0x2b and 0x2c to 0). */
    const struct cok_rules_address_value {
        uint16_t address, value;
    } *start_stores;
    unsigned start_store_count;
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
    /* MUSIC n's sound for n 1-6 (dkk 3632:2878: 15dc:0000 with DS:n+1,
     * DS:0002-0007; n past 0x7f is song n & 0x7f, others nothing). */
    uint8_t music_sounds[7];
    /* WHO's prompt (dqk 6c70:2fe0): a prompt equal to who_prompt, ignoring
     * case (24de), is shown as who_prompt_instead; NULL, none. */
    const char *who_prompt, *who_prompt_instead;
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
    /* Whether a CPIC's icons are not recoloured (Dark Queen's: its loader,
     * 204c:00df, is not traced, and its colour tables are not in the data:
     * the icons load as the library holds them). */
    bool icons_unrecoloured;
    /* Whether the party's combat icons are loaded as the characters join
     * (cok_arena_join: Champions', Death Knights'). Dark Queen's are loaded by its own
     * hooks (dark_queen.c's member_icon: its load, import and Add Character). */
    bool party_icons;
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
     * casting's. Death Knights' band C kinds: COK_CAST_HEAL's value the
     * effect cured after blindness (dkk 5dc5:3848: 0x3d); COK_CAST_DESTROY's
     * the status it kills with (3bb9 6, 40a6 7, saying text);
     * COK_CAST_DISPEL_EVIL's, COK_CAST_ENTANGLE's and COK_CAST_FEEBLEMIND's
     * the effect added to the caster, added to each target, run after
     * (4109 0x04, 42f1 6, 4a04 0x44); COK_CAST_DANCE's value and count the
     * two effects it adds (4dd8: 6 and 0x39). */
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
     * several ids (COK_FX_CANCEL's effect; dkk COK_FX_STUN_HIT's, the
     * effect it adds, 504d:0124's 6), or 0; and the words it says
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

/* The stat recompute (cok_effects_ability): Champions' (60f4:1743) or Dark Queen's
 * (148f:1dfc, a routine of its own: the stats 1 and 2 stored, the constitution's effect 0x3e,
 * and the 18/xx strength as the strength effects give it). */
typedef enum { COK_ABILITY_CHAMPIONS, COK_ABILITY_DARK_QUEEN } cok_ability_routine;

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
    /* Dark Queen (dqk 0369's and 02ed's differences; 0 or NULL in the others). The
     * id whose handler gets the selected character's record (DS:ad67, the one acting)
     * in place of the caller's, when the target holds it (0369's 0xd4). */
    uint8_t acting_id;
    /* 02ed's magic resistance is run whenever an effect is pending or the damage is
     * magic, with no test of the amount; its roll needs the caster level, which is not
     * ported (dqk 177e:1900): where resist_unported is set, a holder of resistance rolls
     * stops there, cited. */
    const char *resist_unported;
    /* 2a6d: after event 6's list, the record's byte at this offset (0 none) makes it
     * immune to the damage with 0x80 (bit 0x10) or 0x40 (bit 8): the damage and the
     * effect pending are cleared. */
    uint16_t damage_flags_at;
    /* The stat recompute (cok_effects_ability): Champions' (60f4:1743) or Dark Queen's
     * (148f:1dfc). Removing FRIENDLY recomputes charisma, ENLARGED and STRENGTH strength, and
     * removing ability_low_id intelligence and wisdom (148f:017d). */
    cok_ability_routine ability_routine;
    /* Dark Queen's 1dfc: the strength effect 0x71 merged with the others (148f:1f92), and the
     * effect that makes intelligence and wisdom 3 while held (0x44, 148f:21e1). 0 for none. */
    uint8_t ability_second_strength_id, ability_low_id;
    /* Dark Queen's 1dfc: the effect a constitution of 20 and up holds (0x3e, 60 minutes, value
     * 0xff, flag 1); 0 for none. */
    uint8_t constitution_id;
    /* Dark Queen's heal (148f:25f7): without the flag a holder of this effect is not healed
     * (0xd7). 0 for none. */
    uint8_t heal_guard_id;
    /* 19c2: an effect of id 0 adds nothing, and the effects' pool holds pool_size nodes
     * (0 unlimited); past it the add stops, cited by pool_unported. */
    unsigned pool_size;
    const char *pool_unported;
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
    /* dqk 177e:0be2: Champions' with the armour class in six parts (a
     * cloak's, type 7, its own), the movement as 177e:0a5f gives it (the
     * effects that double or halve it, the party's variables 0x1c and 0x3c
     * counting as 0 in the port's stats) and no fighter level. */
    COK_STATS_DARK_QUEEN,
    /* No recompute: the record as it is. */
    COK_STATS_NONE,
} cok_stats_routine;

typedef enum {
    /* 66c2:0433: spells a day to level 5 (66c2:000f), the former classes
     * of a human who changed class. */
    COK_LEVELS_CHAMPIONS,
    /* dkk 69a8:050b: spells a day in the spells' block to level 8 and
     * paladins' (69a8:0000), a fourth attack, no former classes. */
    COK_LEVELS_DEATH_KNIGHTS,
    /* dqk 8bcc:07f1 (queen_levels.c): THAC0 by the tables, the highest
     * level and the attacks, the spells a day and those known, the saving
     * throws, the thief's skills and the classes' items usable, run where
     * the original runs it (an import, joining the party, training: not a
     * file's read, nor a saved game's). */
    COK_LEVELS_DARK_QUEEN,
    COK_LEVELS_NONE,
} cok_levels_routine;

/* One run of bytes an import moves from the other game's record to the
 * game's (or to its spells' block). */
typedef struct {
    uint16_t to, from, count;
} cok_rules_move;

/* The routine of an import (party.c's cok_character_import). */
typedef enum {
    /* Death Knights' of Champions' characters (dkk 46b2:10d7): below. */
    COK_IMPORT_DEATH_KNIGHTS,
    /* Dark Queen's of Death Knights' (dqk 604b:0b82): the record read
     * whole (0xd8 bytes or the import fails, 604b:0cd7) into a record
     * zeroed (0x19e), moved run by run (604b:0d65), the name made a C
     * string, a class 7 made 1, the levels of class 7 moved to class 1
     * and those of 7-13 cleared; then its effects (.eff, 604b:16f8: at
     * most 0x8c bytes, each id by effect_ids), its items (.jnk, 604b:181b:
     * at most 0x3f0 bytes, each converted into Dark Queen's item, its name
     * words by word_ids and its spells by spell_ids, the items of type
     * 0x36 dropped) and its spells (.wiz, 604b:2021: under 0xdd bytes, the
     * memorized by spell_ids, the known as bits, the spells a day's four
     * rows to rows 0, 1, 3, 2). */
    COK_IMPORT_DARK_QUEEN,
} cok_import_routine;

/* How a game imports another's characters (party.c's
 * cok_character_import), as Death Knights does Champions' (46b2:10d7 with
 * DS:c468 0): the other game's record, read into a zeroed record of its
 * size (from's layout) from the other game's saves (from's extensions),
 * moved run by run into a zeroed record; the spells' block moved too, for
 * a class not among no_spells; the age capped by race; the items read but
 * those of the dropped types, and the effects, as they are; then
 * recomputed as a load does. */
typedef struct {
    cok_import_routine routine;
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
    /* COK_IMPORT_DARK_QUEEN's maps of the other game's ids to the game's,
     * the cases of the original's switches, {from, to}; an id none lists
     * is kept: spells (dqk 604b:1ea3), effects (604b:1cc7), and the words
     * of items' names (604b:1bf1). And the scrolls' types, whose three
     * spells are spells (6fd8:43f7: 0x27, 0x28, 0x49), ended by 0xff. */
    const uint8_t (*spell_ids)[2], (*effect_ids)[2], (*word_ids)[2];
    size_t spell_id_count, effect_id_count, word_id_count;
    const uint8_t *scroll_types;
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
    /* Each class's bits of the classes whose items one can use, which a
     * monster read from a library (archives.monster_library) is given for
     * its classes with levels (dqk 604b:257c, DS:63f3: classes 0-6); 0
     * where the record keeps its own. */
    uint8_t class_usable[8];
    /* The item types (+type) the stats treat as readied arrows and
     * quarrels (slots 11 and 12; 6346:0d20, dqk 177e:0be2 where the
     * quarrels' are 0x0c or 0x4a, 0 for none), and the weapons a race has
     * a bonus with (6346:0023, dqk 177e:0016): an elf or half-elf with
     * those above elf_first and below elf_last or elf_a or elf_b, a kender
     * with kender. */
    uint8_t arrows, quarrels, quarrels_also;
    uint8_t elf_first, elf_last, elf_a, elf_b, kender;
    /* Whether the strength bonuses to hit and damage count only where
     * the record's STRENGTH_BONUSES byte is set (Champions' +0x114;
     * dqk 177e:1168 and 11f9 test none). */
    bool strength_flag;
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

/* Initialize Mouse/Joystick's file and words (4def:5cc7, 638d; dkk
 * 49cb:52cd, 58ec), the routine itself shared: the file it reads and
 * writes again (both krynn.cfg, CS 5c61 and dkk 52bd: Champions' own
 * devices' file, but not Death Knights', whose DKK.CFG it never touches);
 * its list's prompt and word, its rows (the mouse on and off, the stick on
 * and off, both off), and the calibration's screens: the corners' two
 * lines each, and the failures: the button never pressed, and a read lost
 * while it is held at the upper left and at the lower right (Champions'
 * one line for all three, CS 632b; dkk DS:1b80, CS 58d4, DS:1b80). */
typedef struct {
    const char *file;
    const char *prompt, *select;
    const char *mouse_on, *mouse_off, *stick_on, *stick_off, *both;
    const char *corners[2][2];
    const char *no_button, *lost_upper_left, *lost_lower_right;
} cok_rules_mouse_menu;

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
    /* The title's song for a game with songs (dkk 35c6:051c: 15dc:003d
     * with 1), and whether it plays on through the title's menu, main
     * stopping it at each round of its loop (dkk 1000:015b: 15dc:00a4)
     * and hooking the effects driver for its first sound only (15dc:0000),
     * where Champions' title stops its music (2f55:006d) and main hooks
     * the effects driver after it (1000:0160). */
    uint8_t title_song;
    bool music_past_title;
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
    /* Initialize Mouse/Joystick's (cok_start_mouse). */
    cok_rules_mouse_menu mouse;
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
    /* dqk 59c7:0e3b: Death Knights' floor, river (0860, var COK_VAR_RIVER 0x33),
     * trees (09e7, 0beb's code) and scatter (0b79, 0f00's bands), except the
     * scatter's cell (0ce3): its 0x2c band is one roll wider and its 0x2c + d5
     * band never comes (the second check repeats the first). The flags are
     * DS:5d7b's at COK_VAR_ABYSS's value (DS:a960, stored by 0e3b; 32 of them). */
    COK_WILDERNESS_DARK_QUEEN,
} cok_wilderness_routine;

/* The placement's routine (combat.c): the shared one (cok_combat_place_all, 3cb2:17f7 and
 * dkk 410b:1aa4 with its descriptor fields), or Dark Queen's own (dqk 59c7:1864, the
 * jump table at 1abf and the layouts per record, cok_combat_place_dark_queen). */
typedef enum {
    COK_PLACEMENT_SHARED,
    COK_PLACEMENT_DARK_QUEEN,
} cok_placement_routine;

/* Dark Queen's combat block (59c7:0f7b): the 0x18 bytes a record's far pointer
 * +0x40 keeps, beside the port's cok_combat_record (party.h), which holds the same
 * meanings at its own offsets. Its named bytes, by their offset in the block; the
 * others are unnamed (0x01-0x09, 0x0c-0x13, 0x15: read as 0 by eclplay's --blocks, and
 * checked as 0 at the placement by dqk_fight). The records' differences
 * from cok_combat_records (59c7:0f7b) are here too: a record with the effect
 * facing_effect faces 2, and a record that is not hostile gets gate_effect when
 * gate_var is set. */
typedef struct {
    uint8_t spell;         /* +0x00: a spell being cast; 177e:09ef shows "(Casting)" while
                            * it is above 0 (as Champions' spell). */
    uint8_t facing;        /* +0x0a: the facing (0f7b:1019, 1031; read by 5045:10b4 and
                            * 52db:0b8c, among others). */
    uint8_t not_party;     /* +0x14: set past the party's size (0f7b:0fec; read by the
                            * placement's unlinking, 59c7:1d34-1d94). */
    uint8_t in_battle;     /* +0x17: set to 1 for every record (0f7b:10ca); no reader yet. */
    uint8_t facing_effect; /* 0xd5 (0f7b:0ff6, 103f): facing 2 at once. */
    uint8_t gate_effect;   /* 0x95, added by 148f:19c2 (0f7b:10f4). */
    uint8_t gate_var;      /* a cok_var (ecl.h): 0x3c, which gates gate_effect (0f7b:10cf). */
    /* The round's start writes these (52db:0004; eclplay's --blocks has them): the combat record's
     * fields at their offsets here. initiative is a word (initiative_high its high byte, 0 or 0xff
     * for a negative one); attack_slot, movement and attacked are the record's +4, +6 and +8 in
     * the port. */
    uint8_t may_cast, may_use, initiative, initiative_high, attack_slot, movement, attacked;
    /* The combat record's guarding, dying, hits, forced, turns and turning bytes (dqk +0x08,
     * +0x0f, +0x10, +0x11, +0x12, +0x13: the port's cok_combat_record's, one byte on). Set by
     * Dark Queen's command menu (queen_commands.c) and read by its conditions and the
     * placement's checks; eclplay's --blocks writes them. */
    uint8_t guarding, dying, hits, forced, turns, turning;
    /* The combat record's pattern (+0x16: the way of moving, 5bb8:0000's 008d) and target (+0x0b:
     * a far pointer to the record it aims at). eclplay's --blocks writes the pattern, and the
     * target's place in the party list plus 1 (0 for none) at +0x0b: the port has no pointers, and
     * dqk_fight masks the original's to the same. */
    uint8_t pattern, target;
} cok_combat_block;

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
    /* The distance where Dark Queen's differs (dqk 6c70:01c3, 2db1): with no 3D
     * area the open squares (01c3's 3d == 0 branch) set var COK_VAR_DISTANCE_WANTED,
     * not COK_VAR_DISTANCE, as Champions' and Death Knights' do; and a distance
     * wanted of 16 is 2 squares, both vars 2 (2db1's 2e1c). The sprite and close-up
     * at a distance are the drawing's (cok_drawing.encounter). */
    bool open_squares_wanted, distance_16_is_2;
    /* The battle (3995:0172; dkk 3e05:00ec): whether leadership (dkk
     * 5340:4acf) comes before the setup, with its words (the first line,
     * then name, leads, name and the end); whether after each turn and
     * each blow the dead explode (60f4:2375; dkk has none); whether after
     * each turn a side gone or the round limit ends the round there, once
     * a battle (dkk 3e05:00ec's [bp-1]), rather than only at the end of the
     * round. The aging after it is the levels section's. */
    bool leadership;
    const char *leadership_text, *leads, *leads_end; /* NULL text: no announcement (dqk) */
    /* Leadership's descriptor differences (dkk 5340:4acf; dqk 52db:4dd2, 4d50): the
     * index of the class level the leader reads (COK_FIELD_CLASS_LEVEL: dkk 7, dqk 1);
     * the computer flag an NPC gets (dkk 1; dqk 0x80 at +0x18c, from its +0x9b bit 7);
     * and whether the chance its knights of no order use is kept from one call to the
     * next (dkk's [bp-3]; dqk's 4d50 has it in its own frame, unset). */
    uint8_t leadership_level, leadership_computer;
    bool leadership_chance_kept;
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
     * dungeon_floor (dkk 410b:10d7 and 0892: 0x16; dqk 59c7:06a7, 0x16 too);
     * whether a door on the north-west corner's west edge (door_corner_nw:
     * dkk 410b:04d8; dqk 59c7:0285 says no) and beyond the north-east
     * one's north (door_corner_ne: dkk 410b:06ae; dqk 59c7:049e says yes)
     * is drawn as a wall's corner is; whether the dungeon has tables and
     * chairs in its rooms (dungeon_no_tables: dqk 59c7:06a7 places none,
     * and draws no dice there); the wilderness and placement the port does
     * not carry out yet (the stops' citations, dqk 59c7:1864 for the
     * placement; NULL for none; Dark Queen's wilderness is ported, so NULL);
     * whether the setup clears Magic On (DS:7198) and the dead
     * that explode (DS:6b45, 6b95; dkk neither). */
    const char *begins;
    uint8_t begins_colour;
    /* Whether the setup's prelude is skipped (cok_combat_setup: the pictures freed,
     * row 24 cleared, the wait and "A battle begins..."): dqk 5045:059d and 59c7:1dd7
     * do none of them; the battle's text is COMBAT's (monster.c). */
    bool no_setup_prelude;
    uint8_t dungeon_tiles, wilderness_tiles;
    bool tiles_kept;
    uint8_t wilderness;
    bool dungeon_fill;
    uint8_t dungeon_floor;
    bool door_corner_nw, door_corner_ne;
    bool dungeon_no_tables;
    const char *wilderness_unported;
    /* The placement's routine (above). */
    cok_placement_routine placement;
    /* The setup's tail after the arena's redraw (dqk 59c7:1e99, 1dd7's tail; Champions' and
     * DKK's is cok_combat_setup's). Dark Queen's gives each record effect 8 only (1ea5-1ed3, no
     * 0x16) and clears its block's turning byte (+0x12), not turns (+0x11); and 1dd7 clears Move
     * mode (DS:af3d) and leaves the cursors (DS:71a5, 71a6) as they are: setup_effect_8_only,
     * setup_clears_turning and setup_keeps_aim.
     * The tail also clears combatant 0's x (DS:c3f1, its first byte) and DS:b2ea (the caster's
     * level, read by the magic resistance 148f:02ed). The port does not model either yet: nothing
     * before the first turn reads them, and a part that writes combatant 0 or casts (10b on) must
     * clear them here. */
    bool setup_effect_8_only, setup_clears_turning, setup_keeps_aim;
    /* The movement is Dark Queen's (dqk 177e:0a5f and 01ef: the base movement at +0x90, the readied
     * items' weights, the effects 0x27, 0x2a, 0x4a, 0x96 and 0xb2, and the party's var 0x1c; no
     * event 0x12 of its own, which 52db:0004 makes before it). */
    bool movement_from_base;
    /* Dark Queen's may cast (dqk 52db:0004's +1): a record may cast only with a memorised spell,
     * a byte of 1 to 0x7f in its 0x89 bytes from the memorized field (+0xce), and not in a cloud. */
    bool may_cast_needs_spell;
    /* Dark Queen's may use (52db:0004's +2, the probe's cloud): a record may use items only out
     * of a cloud. */
    bool may_use_needs_no_cloud;
    /* Dark Queen's ammunition (51c2:10c3): none while var 0x3c (COK_VAR_RECORD_EFFECT) is set,
     * and a weapon whose type's flags byte 14 is ammunition_also (0x0c; 0 for none) has some as
     * well as 0x0a. */
    bool ammunition_gated;
    uint8_t ammunition_also;
    /* The listing's bytes that the distance (51c2:0b26, cok_combat_distance) puts back after it
     * (1df9:0432 -> 10d7:2776 moves 4 bytes: DS:c2f3[0..3], entry 0 and entry 1's index; Dark
     * Queen's 4). 0, the default, puts back the whole listing but its first entry (Champions',
     * Death Knights'). */
    uint8_t distance_kept_bytes;
    /* The enemy health (52db:29f1, the side count's own test at 51c2:094c): a record of status 9
     * is left out of it (dqk; Champions and DKK count it). */
    bool enemy_health_skip_status_9;
    /* Each turn's dispatch (dqk 5045:078b, after 51c2:0920, 52db:0004 and 5045:0447): a
     * computer's turn is 5bb8:0000 and a party's is the command menu, 5045:08cf, for a record
     * that can act (+0x18b). A record that cannot act is ended at once (51c2:0c11, its turn
     * ended); one with a spell pending (its combat block's spell) is cast at 849a:145b
     * (spell_unported) instead of the menu. Reaching a stop returns with its citation (NULL:
     * none). menu_first: the party's turn is the commands' routine (cok_combat_commands, the
     * command menu, 5045:08cf) and is dispatched before --combat's stubs, so no stub passes a
     * turn over it. The computer's stop is checked before the stubs too.
     * After each turn (5045:059d's tail, the pending lists and the demonstration's key test;
     * cok_combat_turn_tail), the sides and ends_in_round's early end. The round's end and the
     * battle's end are Dark Queen's own routines in the descriptor's hooks below (5045:0f6c,
     * 5045:0001), not the shared ones. */
    bool menu_first;
    const char *computer_unported, *spell_unported;
    /* Dark Queen's turn tail (5045:059d's, cok_combat_turn_tail): a pending list (cok_combat's
     * pending counts, filled by handlers 0x9a, 0xbc and 0xbd, not yet ported) stops here, cited;
     * so does the demonstration's key test (5045:06cf-070e). The colour cycles step after them
     * (cok_drawing's turn_tail_cycles). */
    const char *pending_unported, *demo_key_unported;
    /* Dark Queen's round end (5045:0f6c; the drawing's hooks round_clock, teammate_dying and
     * continue_battle where set): the clouds stop at round_clouds_unported when one exists, and
     * the square test (148f:12ab) stops at square_unported for a cloud kind 0x1b or 0x1c, or
     * acid (0x46: cok_combat_marks). */
    const char *round_clouds_unported, *square_unported;
    /* Dark Queen's battle's end (5045:0001, the shared cok_combat_end): each record holding one
     * of the two effect ids (0 for none) stops at battle_end_unported (148f:017d, part 27b)
     * before its charm test; the drawing's battle_icons empties the combat icons (204c:004d).
     * aging_unported stops in rounds() where the aging report (5045:012f) is due
     * (effects.aging), instead of cok_train_aging. A battle over before any round also runs
     * the battle's end. */
    const char *battle_end_unported;
    uint8_t battle_end_effects[2];
    const char *aging_unported;
    /* The placement (3cb2:17f7; dkk 410b:1aa4 and 18c2): the entries whose
     * sizes it clears first, 1 to placement_clears (255, into the screen
     * positions; dkk 72); whether SETUP MONSTERS' ambush directions
     * (game->ambush, dkk DS:c4cc) split the enemy's side among them, each
     * enemy facing from the next, and each record's combat facing is set
     * here; the enemy's distance where it is 0 and the facing is not the
     * party's (dkk 1; dqk 59c7:16b3's flag is 1 too), and in the demonstration
     * (dkk 2; 0 none); whether a record laid out again for its facing is placed
     * as the enemy's side (dkk 410b:18c2's loop leaves DS:86e8 1, true; dqk
     * 59c7:16b3's loop is a local and leaves the record's own side, false);
     * whether a body's cell keeps the cell under it rather than the map's
     * (dkk 6f03:152e: under a body already there, the body's);
     * and whether a record of status 9 is taken off the map (3cb2:17f7; dkk
     * none). */
    uint8_t placement_clears;
    bool ambush, layout_sets_side;
    uint8_t ambush_distance, demo_distance;
    bool body_under, status_9;
    /* Whether the side count (6346:268a; dqk 51c2:0920) leaves out a record of status 9
     * (its test at 51c2:094c; dkk and Champions count it). */
    bool sides_skip_status_9;
    /* Whether the side count takes one from side 1 while the ambush byte (DS:c2e7) is 0x20
     * (dqk 51c2:0920's 096a-0978, every call: the rounds' counts too; dkk and Champions
     * do not). */
    bool sides_ambush_0x20;
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
    /* Where LOAD MONSTER only registers its monsters and COMBAT appends them
     * (dqk 6c70:29a3, then 5f07:081b's monsters branch 6c70:2d82 and 170e):
     * the records of one slot up to monster_records_max in all (dqk 29fc's
     * 0x28), the extra ones up to monster_records_extra (170e's 0x32); the
     * extra monsters by var extra_monster_var (dqk 0x3a) are the pairs
     * {id, icon} of extra_monsters, extra_monster_count of them. False where
     * LOAD MONSTER appends at once (Champions, Death Knights). */
    bool monsters_at_combat;
    uint8_t monster_records_max, monster_records_extra;
    const uint8_t (*extra_monsters)[2];
    uint8_t extra_monster_count, extra_monster_var, extra_monsters_appended;
    /* COMBAT with no monsters loaded, the treasure branch (dqk 5f07:081b's last: 75cc:135c),
     * which the game runs itself (its enum; none: the shared end of combat), and the
     * battle's routine the port does not carry out yet (dqk 5045:059d; NULL where the rounds
     * are run). */
    cok_no_monsters_treasure no_monsters_treasure;
    const char *battle_unported;
    /* Where the mode after COMBAT is 3 only on the overland: with no 3D area (var COK_VAR_3D 0)
     * and this address 1 (dqk 5f07:0917-0949 read 0x24; the mode is 4 otherwise), or 0 where it
     * is 3 with no 3D area (Champions', Death Knights'). The overland's mark after a fight (dqk
     * 5f07:0964-0982, 7065:0011) is not ported: its text, or NULL. */
    uint8_t overland_var;
    const char *overland_mark_unported;
    /* Whether a load is asked (Dark Queen's DS:b7a7, 5f07:0974): the mark is not made while it is
     * set (adventure.c's cok_adventure_load_asked); the mark is also not made after an abort. */
    cok_load_asked load_asked;
    /* Whether the temple's variable COK_VAR_TEMPLE is cleared after the temple runs (dqk
     * 5f07:0861-0871: it reads 1 meanwhile); Champions' and Death Knights' clear it first. */
    bool temple_var_after;
    /* Dark Queen's combat block and the records that differ (59c7:0f7b), or NULL where
     * cok_combat_records is the records (3cb2:10d9, dkk 410b's). */
    const cok_combat_block *combat_block;
    /* Dark Queen's status and hostile are ints (5045:0001's cmpw, 0f6c's and 51c2's cmpw, 1b66's
     * movw): the battle's end and the round's end test and write the word at the byte field
     * (cok_combat_field_word, cok_combat_set_field_word). */
    bool int_status_hostile;
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

/* The computer's turn (cok_combat_computer, ai.c's dispatch), each the port's routine of that
 * game's original. */
typedef enum {
    /* 3afb:004b: ai.c's cok_combat_computer. */
    COK_COMPUTER_SHARED,
    /* dqk 5bb8:0000: queen_computer (queen_ai.c), the frame that picks, casts, turns undead and
     * chooses a weapon; each step its own, with the shared routines it calls. */
    COK_COMPUTER_DARK_QUEEN,
} cok_computer_routine;

/* The computer's keys (ai.c's cok_ai_keys), each the port's routine of that game's
 * original. */
typedef enum {
    /* 3afb:1200: '2' toggles Magic, Space gives the turn back, '-' the gods with the Helm
     * (cok_ai_keys reads one key, then one more for a 0). */
    COK_KEYS_CHAMPIONS,
    /* dqk 5bb8:1050: the keys waiting through queen_read_code (2093:0078's codes, every key
     * read and the last kept): Space as above, Alt-M (0xcc) toggles Magic with its notice, Alt-X
     * (0xd7) gives the gods (52db:3feb, whose own Helm test the port reads as game->helm, as its
     * callers do). */
    COK_KEYS_DARK_QUEEN,
} cok_computer_keys;

/* The computer's may-step test (ai.c's cok_ai_can_step), each the port's routine of that game's
 * original. */
typedef enum {
    /* 3afb:0743: the shared test (the probe, the cloud's saving throw and the puddle). */
    COK_CAN_STEP_CHAMPIONS,
    /* dqk 5bb8:0438: the probe and marks of the square, the cost, and the effects and bits that
     * free a cloud (queen_can_step, queen_ai.c). */
    COK_CAN_STEP_DARK_QUEEN,
} cok_computer_can_step;

/* The computer's turn with no movement left and not fleeing (ai.c's no-move tail), each the
 * port's routine of that game's original. */
typedef enum {
    /* The guard (cok_ai_guard). */
    COK_NO_MOVE_GUARD,
    /* dqk 5bb8:0af0: the spell choice first (5bb8:0306, queen_spell_choice), whose cast ends the
     * turn; then the guard. */
    COK_NO_MOVE_DARK_QUEEN,
} cok_computer_no_move;

/* The to-hit bonus of a roll (cok_combat_hit), each the port's routine of that game's original. */
typedef enum {
    /* 60f4:10xx: COK_VAR_HIT_BONUS's two bytes, the monsters' (0) and the party's (1), signed. */
    COK_HIT_BONUS_PAIR,
    /* dqk 148f:1771's step 4: the party's var COK_VAR_HIT_PARTY (its hostile word 0) as a signed
     * byte plus the difficulty's adjustment (COK_VAR_DIFFICULTY 1 +2, 2 +1, 3 0, 4 -1, 5 -2, else
     * 0); the monsters' COK_VAR_HIT_MONSTERS. */
    COK_HIT_BONUS_DARK_QUEEN,
} cok_hit_bonus;

/* The backstab (cok_combat_backstab and the damage's times), each the port's routine of that
 * game's original. */
typedef enum {
    /* The port's (Champions'; Death Knights' as the port has it): a thief by its level, or by a
     * former class's level when the former counts; the damage times 2 + (level - 1) / 4 as a
     * byte. */
    COK_BACKSTAB_FORMER,
    /* dqk 52db:278f: a thief by its level or effect backstab_effect; the damage times 5 for level
     * 0, else (level - 1) / 4 + 2, at most 5, as a word (52db:01dd). */
    COK_BACKSTAB_DARK_QUEEN,
} cok_backstab_routine;

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

/* A kind of missile's pictures (432f:2beb; dqk 52db:2888): the icon slot they are drawn from, and
 * the sound made after them. */
typedef struct {
    uint8_t slot, sound;
} cok_missile_kind;

/* The missile's pictures by kind (attack.c's cok_combat_missile_pictures): an arrow's (the item
 * types in fight.arrows), a spinning weapon's (types 2, 3, 6), a thrown weapon's (0x36, 0x37),
 * a sling stone's (0x1c, 0x1d, 0x43) and another's. The arrow's slot is that of the even
 * directions' pictures; the odd ones use the next slot. A thrown weapon's slot 0 is none (Dark
 * Queen's, where those types are another's); lead_sound makes the missile sound first (0x0c,
 * 432f:2beb: Champions' and Death Knights', not Dark Queen's). */
typedef struct {
    cok_missile_kind arrow, spin, thrown, stone, other;
    bool lead_sound;
} cok_rules_missile;

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
     * it); the death's sound (5; Dark Queen's 5837:0d55 sounds 6); the
     * cells a body is laid over and keeps, 0-ended (NULL: cloud_tile, and
     * pool_tile where body_spares_pool; Dark Queen's 5837:0d55 keeps 0x1d,
     * 0x1c and 0x46); the status with no body (0 none; Dark Queen's 8, the
     * record not laid down); the status whose body is laid as the tile
     * kill_stone_tile (0 none; Dark Queen's 7 gives 0x1e, a stone); the
     * cells 5837:137a looks through for the cell under a body, 0-ended
     * (NULL: cloud_tile and pool_tile; Dark Queen's 0x1d, 0x1c, 0x1b and
     * 0x46). */
    bool kill_finds_body, kill_clears_spell;
    uint8_t kill_sound;
    const uint8_t *body_spared;
    uint8_t kill_no_body_status, kill_stone_status, kill_stone_tile;
    const uint8_t *under_tiles;
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
     * is compared signed (dkk, a byte, and Dark Queen's 5bb8:1117) rather than as an
     * unsigned word, whether the morale roll is capped at 0x66 (Champions and Death Knights;
     * Dark Queen's 5bb8:1117 is not), and whether one made to flee loses effects
     * COK_EFFECT_DISPELLING and GIANT_SLAYER (Champions). */
    bool morale_signed, morale_uncapped, flee_removes;
    /* The computer's turn (cok_computer_routine): Dark Queen's 5bb8:0000 (queen_ai.c). */
    cok_computer_routine computer_routine;
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
    /* The missile (432f:2beb; dkk 5340:28a0; dqk 52db:2888): the item types drawn
     * as arrows, 0-ended; the pictures by kind; whether its flight's ends are first kept on the
     * battlefield (dkk 65cd:1d56, dqk 51c2:00df: x 0-0x31, y 0-0x18). Dark Queen's flight draws
     * its frames through the drawing's arena_missile_* hooks, and waits 2 * delay per frame
     * (missile_wait_twice: 1df9:052a(delay * 2)), where Champions' and Death Knights' wait delay
     * (1962:029c). */
    const uint8_t *arrows;
    cok_rules_missile missile;
    bool missile_clamped;
    bool missile_wait_twice;
    /* The icon slot of the burst drawn on one hurt (6346:228c with kind 0:
     * 0x17; dkk 65cd:24bc 0x18; kind 1's flash is 0x16 in both). */
    uint8_t burst_slot;
    /* Turning undead (cok_turning_routine). */
    uint8_t turning;
    /* The computer's "Move/Attack, Move Left = N" on row 24 (3afb:095a): its colour (10, light
     * green; dqk 5bb8:06b0's 7), and whether it has no trailing space from ten up (dqk 5bb8:06b0
     * takes its format 5e0a for ten or more, 5ded, with the space, below). */
    uint8_t moves_left_colour;
    bool moves_left_bare_from_ten;
    /* The computer's keys (cok_ai_keys): the routine (cok_computer_keys), and whether the
     * keyboard is left unflushed after a key (Dark Queen's 5bb8:1050 does not flush: 3afb:12eb
     * does, for Champions). */
    cok_computer_keys keys;
    bool keys_unflushed;
    /* The pick of a target (cok_combat_pick_target_side; dqk 52db:3d9e): with the tries the
     * number listed, not 20, after one draw that is not used (Dark Queen's). */
    bool pick_tries_listed;
    /* The step's sound (52db:06c0's 0x0b for Dark Queen; 0x0a, DS:1e5c, for Champions and Death
     * Knights). */
    uint8_t step_sound;
    /* Whether a class 5 holds whatever its slot 2 (dqk 5bb8:06b0); false tests that slot too
     * (3afb:095a, dkk 3f63:08c6). */
    bool step_no_slot_test;
    /* The computer's may-step test (cok_computer_can_step) and its no-move tail
     * (cok_computer_no_move). */
    cok_computer_can_step can_step;
    cok_computer_no_move no_move;
    /* Whether the ambush byte at 0x20 (game->ambush) blocks a flight (dqk 52db:0e26: "Escape is
     * blocked" with no test of the others'). */
    bool flee_blocked_by_ambush;
    /* The effects that bar a free attack's blow (dqk 52db:08e6's 177e:1734 tests), 0-ended, or NULL
     * (Champions' and Death Knights' have none): the blow is not made, the target kept, and the
     * turn's free attack used up. */
    const uint8_t *free_attack_barred;
    /* The to-hit bonus of a roll (cok_hit_bonus; 148f:1771's step 4). */
    cok_hit_bonus hit_bonus;
    /* The sounds of a strike's hit and of its miss (52db:1417: 8 and 0x0a for Dark Queen; 7 and 9
     * for Champions and Death Knights), and whether a miss sounds only with no item
     * (52db:1417's 17c5; Champions' always does). */
    uint8_t hit_sound, miss_sound;
    bool miss_sound_no_item;
    /* Whether the damage is a signed word (DS:ae55: cok_rolls.amount16, kept by 52db:01dd and
     * 1417 and reported at 02c0 as a word, a helpless target's hp + 5 too); Champions' and Death
     * Knights' is a byte (DS:6b30). */
    bool damage_word;
    /* The backstab (cok_backstab_routine): the readied weapon's item types that allow one, 0-ended
     * (dqk 52db:278f: 3, 4, 0x11-0x13, 0x3f, 0x43), and the effect that makes a thief (dqk 0xbe;
     * 0 none). */
    cok_backstab_routine backstab;
    const uint8_t *backstab_weapons;
    uint8_t backstab_effect;
    /* The memorized bytes that a forgotten spell is looked for in, from the field's first byte: 0
     * the field's own count, Dark Queen's 0x89 (177e:1346 walks past the field's 0x4a). */
    uint8_t forget_bytes;
    /* The effect a holder of which gets event slot + 1 on a strike that hit even for no damage
     * (dqk 52db:1417's 0xb8 test; 0 none). */
    uint8_t hit_event_effect;
    /* Whether a strike keeps the attacker's sweeps left (cok_combat_record's sweeps; dqk 52db:1417
     * clears none). */
    bool strike_keeps_sweeps;
    /* The colour of "and is Dying" on the window's grey (cok_arena_text at 0x17, the row, bg 0:
     * dqk 1a62:0046's 7; Champions' and Death Knights' 10). */
    uint8_t dying_colour;
    /* The stop for an item a strike took and then removes (use_up_freed): the removal writes
     * through NULL for an item not in the list (dqk 177e:13d2). NULL: the port's alert. */
    const char *freed_remove_unported;
    /* A computer's surrender (ai.c's morale, cok_combat_leave with status 4; dqk 148f:1a69 from
     * 5bb8:1117, part 16). The flight (52db:0e26) calls the same routine and is ported, so the
     * stop is here and not in cok_combat_leave. NULL: the port's own. */
    const char *surrender_unported;
    /* Whether the sweep's list is not swapped for one yelled at (dqk 52db:54b5 has no swap;
     * Champions' 432f:0fce does). */
    bool sweep_no_yell;
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
    /* dqk 5045:08cf: the command menu of 0cd7's items (Aim, Use, Guard, Quick, Delay, View, Speed,
     * End, and the others when they hold), shown on the bottom bar in two rows (1bdb:1595), and
     * its routines by index (queen_commands.c). */
    COK_COMMANDS_DARK_QUEEN,
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
    /* Whether a pick (the adventure's, WHO's: dqk 76b8:3ae5) also moves on End and
     * Home, as its table does (the keys 0x85 and 0x87): End to the next, Home to the
     * one before. Champions' and Death Knights' picks do not. */
    bool pick_end_home;
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

/* How a readied item is taken off (DESTROY ITEMS, ROB; items.c's
 * cok_item_unready_routine): Champions' and Death Knights' (cok_item_unready),
 * or Dark Queen's readied branch (queen_view.c's queen_item_unready). */
typedef enum { COK_UNREADY_ITEMS, COK_UNREADY_QUEEN } cok_unready_routine;

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
    /* DESTROY ITEMS (dqk 6c70:30ec): whether its type's high bit destroys only
     * the items with a bonus (+0x30, item_record's bonus), the type being the
     * low seven bits, rather than every item of the type; and whether the
     * stats are recomputed for the members that lost an item only (dqk
     * 6c70:31af), rather than for every member (Champions' 2fd3:35a3). */
    bool destroy_magic, destroy_lost_only;
    /* ROB's money (dqk 6c70:124f): each coin times the percent left, over 100,
     * as integers, rather than as Turbo Pascal Reals (3775:1c7c). */
    bool rob_integer;
    /* The readied item's routine (cok_unready_routine), for DESTROY ITEMS
     * and ROB; Dark Queen's is dqk 86b8:1965's readied branch. */
    cok_unready_routine unready;
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
    COK_SHOPS_DARK_QUEEN, /* the temple (dqk 7825:0af6, dark_queen.c); the shops as Champions' */
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

/* ADD EP's routine (cok_rules_treasure's gains_routine): the one the Champions
 * and Death Knights share (2fd3:36dc, dkk 3632:25f1), or Dark Queen's. */
typedef enum { COK_GAINS_STANDARD, COK_GAINS_DARK_QUEEN } cok_gains_routine;

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
    /* The words' colour (Champions' and Death Knights' 10, Dark Queen's 7:
     * 6c70:2719's push 7), and the routine that gives the points: the one
     * above (COK_GAINS_STANDARD), or Dark Queen's (dqk 6c70:2719: points
     * times 100, divided among the nonzero class levels, see adventure.c). */
    uint8_t gains_colour;
    cok_gains_routine gains_routine;
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
    /* The camp's Fix (camp.c), cok_fix_routine: Champions' (4888:2b44) or
     * Death Knights' (dkk 4381:2fd4, with its cures 2a73 and a second
     * rest). */
    uint8_t fix;
    /* The rest's words (camp.c): the day's healing ("The Whole Party Is
     * Healed", 57e4:09f0; dkk 5b7d:0912's DS:2560) and an encounter's
     * ("Your repose is suddenly interrupted!", 57e4:0e3e; dkk 5b7d:0d51's
     * DS:25a9). */
    const char *party_healed, *interrupted;
    /* The interactive rest's (camp.c): its menu's items (57e4:08a0's
     * CS:0859; dkk 5b7d:081d's DS:324d, by index, the special keys but Up,
     * Down, Left and Right picking Exit); the question asked when a key
     * is waiting ("Stop Resting? ", 57e4:0e3e's CS:0e0a from No; dkk
     * 5b7d:0d51's DS:10a1, 6a69:2048 from Yes, DS:57c0); and whether the
     * camp's Rest starts the menu from its first item (4888:2c31 sets
     * DS:6e0f to 1) rather than from the camp menu's own selection (dkk
     * 4381:3106 leaves DS:c0f7: Rest's fourth, Mins). */
    const char *rest_menu, *stop_resting;
    bool stop_resting_yes, rest_from_first;
    /* The question the camp's Save asks after saving ("Quit TO DOS ",
     * 4888:2c31's CS:2c20; dkk 4381:3106's DS:124c, its start menu's). */
    const char *camp_quit;
    /* The routine the camp's Save cites for the rule book's question every
     * tenth save, which the port logs as unported (4888:0376; dkk
     * 4381:0337, its counter 0x4cf6). */
    const char *copy_protection;
    /* Alter's (camp.c): its menu (4888:2539's; dkk 4381:21df's DS:371e,
     * no Pics); Order's prompt (4888:1fc1's "Party Order: ", over "Select
     * Exit" or "Place Exit"; dkk 4381:1d98's DS:30df) and, by index, its
     * items (dkk DS:33f5: Select or Place, and Exit; NULL in Champions);
     * what Drop says first ("will be gone", 4888:2126; dkk 4381:1f14's
     * DS:110a) and asks with the party's last ("quit TO DOS: "; dkk
     * DS:124c); Speed's line, the speed between its two parts (4888:2384's
     * "Game Speed = " and " (0=fastest 9=slowest)"; dkk 4381:20d3's
     * DS:168e and none), its prompt ("Game Speed:"; dkk DS:30f4) and, by
     * index, its items (dkk DS:340a; NULL in Champions, which lists those
     * offered). */
    const char *alter_menu, *order_prompt, *order_menu, *drop_gone, *drop_quit;
    const char *speed_before, *speed_after, *speed_prompt, *speed_menu;
} cok_rules_levels;

/* The camp's Fix (cok_rules_levels' fix; see Camp and Death Knights'
 * camp): Champions' heals from the cure spells memorized and those the
 * clerics memorize in one rest (4888:2b44); Death Knights' heals too, and
 * cures the stoned, poisoned, dead, cursed and blind, from the spells
 * memorized and, after a second rest, those memorized in it, its rests
 * learning nothing (dkk 4381:2fd4, DS:c46f). */
typedef enum {
    COK_FIX_CHAMPIONS,
    COK_FIX_DEATH_KNIGHTS,
} cok_fix_routine;

/* Modify's most maximum hit points (cok_rules_creation's most). */
typedef enum {
    COK_MOST_CHAMPIONS,
    COK_MOST_DEATH_KNIGHTS,
} cok_most_routine;

/* A class id's start in Pick Class (4def:0ae0; dkk 49cb:05c7): its levels
 * by class (CLASS_LEVEL 0-7) and its experience. */
typedef struct {
    uint8_t levels[8];
    uint32_t experience;
} cok_rules_new_class;

/* The spells a mage of a level knows once creation's training reaches it
 * (4def:4d9e with DS:4b58; dkk 49cb:43b6 with DS:8c5c): for the mage
 * level, and the order of magic (+0x5e) or 0 for any order not listed
 * before, up to four spell ids, 0-ended. */
typedef struct {
    uint8_t level, order, spells[4];
} cok_rules_created_spells;

/* Starting items by kind (dkk 49cb:4bea): the item table's ids (ITEM0's,
 * the treasure section's table), 0 for none. */
typedef struct {
    uint8_t items[4];
} cok_rules_kit;

/* Creating a character, Modify Character and the icon editor where the
 * games differ (create.c, modify.c, icon.c; see Creating characters).
 * Their lists and menus go by letters or by index as the menus section's
 * routine does (dkk 6a69:1b82, 0f1b, 2048). */
typedef struct {
    /* The lists' headings: race, gender, class, deity, alignment
     * (4def:07a0, 0a1d, 0ae0, 0f8e, 1222; dkk DS:1280, 128b, 12a0, 12ab,
     * 12b6). */
    const char *headings[5];
    /* By class id, 0-16, its levels and experience (Champions' 4def:0ae0;
     * dkk 49cb:05c7: 210003 for one class, 105002 for two, 70001 for
     * three, a ranger at level 2), and the effect a paladin (class 3) gets,
     * 0 for none (dkk 0x2d, protection from evil). */
    cok_rules_new_class classes[17];
    uint8_t paladin_effect;
    /* The class ids that get no spells' block, ended by 0xff (dkk
     * 49cb:05c7: fighter, thief, fighter/thief), NULL where the record
     * holds its spells. */
    const uint8_t *no_spells;
    /* The money (4def:20e3; dkk 49cb:05c7): steel by the classes' dice and
     * a fifth more (0 here), or this many steel (dkk 500). */
    uint16_t steel;
    /* The spells a new mage knows (4def:20e3: 11, 18, 12, 15, 21; dkk
     * 10, 11, 12, 15, 16, 18, 19), 0-ended; and those training gives it as
     * it is made, by level (Champions' 2-5 whatever its order; dkk 3, 5
     * and 7, White's or the others'), created_count of them. */
    uint8_t mage_spells[8];
    const cok_rules_created_spells *created;
    uint8_t created_count;
    /* "Reroll stats? " (light magenta, from No), "Character name: " and
     * the question of saving (Champions' "Save %s? " with the name, from
     * No; dkk DS:12cb, 12e0, 12f5 "save character? " from Yes). */
    const char *reroll, *name_prompt, *save;
    bool save_names, save_yes;
    /* What a new character's items are where the game gives every class
     * its own (dkk 49cb:4bea): by kind (1 knight, 2 fighter, ranger or
     * paladin, 3 thief, 4 cleric, 5 mage) the kit, its first item the
     * cleric_thief_item for a thief with a cleric level, each readied but
     * unready_item; NULL where only a knight gets items (4def:5534's). */
    const cok_rules_kit *kits;
    uint8_t cleric_thief_item, unready_item;
    /* Modify Character (4def:28fa; dkk 49cb:25d4): the experiences of a
     * character that may be modified, fresh_count of them, and whether
     * the level it changed class at must be 0 too (the field, where the
     * game has it); its menu's prompt (dkk DS:302d); where its by-index
     * menu leaves strength held at 17 for a race and class (dkk: a hill
     * dwarf paladin, 3 and 3), race 0xff for none; and whether Keep sets
     * SHARES (+0xe8). */
    const uint32_t *fresh;
    uint8_t fresh_count;
    const char *modify_prompt;
    uint8_t charisma_race, charisma_class;
    bool keep_shares;
    /* The most maximum hit points Modify allows (cok_most_routine:
     * Champions' 4def:48e9, cok_modify_most; or dkk 580d:3454, the level
     * drain's, cok_train_most_hit_points). */
    uint8_t most;
    /* The icon editor (4def:408b; dkk 49cb:3cac): "Is this icon ok? " and
     * its Yes first or not (dkk DS:13fd from Yes), "Sorry, not in CGA"
     * (dkk DS:13bd), the icon slot of the frame behind each icon (COMSPR
     * 25's slot 25; dkk 0x1c, 49cb:39d6), the most head and body
     * (4def:408b's 13 and 31; dkk DS:72ac, 72ad). */
    const char *icon_ok, *no_cga;
    bool icon_ok_yes;
    uint8_t icon_frame, heads, bodies;
} cok_rules_creation;

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

/* An item as the port holds it (COK_ITEM_SIZE bytes, party.h) and as the
 * game's files hold it. */
typedef struct {
    /* The bytes of an item its files hold, from file_at in the port's
     * record: Champions' and Death Knights' 63 whole (.STF, .JNK, MON*ITM);
     * Dark Queen's 17 (.QCH, MONCHA, ITEM.DAT), which its item in memory
     * (0x3d bytes: a next pointer at +0, a byte at +4, the name, a C
     * string, at +5, these 17 at +0x28, the contents' pointer at +0x39;
     * dqk 604b:05fd, 604b:181b) holds at +0x28, as the port's record does. */
    uint8_t file_size, file_at;
    /* Where the item's type and its count (how many: arrows, charges, a
     * container's contents) lie in the port's record. */
    uint8_t type, count;
    /* And its weight (a word), the byte that is set while it is readied,
     * its bonus (signed) and its saving throw bonus: what the stats read
     * (6346:0d20; dqk 177e:0be2). */
    uint8_t weight, readied, bonus, save_bonus;
    /* The byte that is set while it is cursed (Dark Queen's +0x34, read by
     * the computer's weapon choice, dqk 5bb8:1271 and 1396, queen_ai.c); 0
     * where the port does not read it yet (Champions' and Death Knights'). */
    uint8_t cursed;
    /* The type whose items are followed in a file by count items, their
     * contents (Dark Queen's scrolls, 0x49: dqk 604b:05fd, 604b:2200); 0
     * for none. */
    uint8_t container;
    /* Where an item record keeps the id of its effect, which the use-up tests for the spiritual
     * hammer (0x17, attack.c): Champions' and Death Knights' +0x3d, Dark Queen's +0x37 (dqk
     * 52db:18d6's 1b26). */
    uint8_t effect;
} cok_rules_item_record;

/* The random numbers' generator (cok_tp_random, ecl.h): Turbo Pascal's
 * RANDOM, which Champions' and Death Knights' use, or Dark Queen's own
 * (dqk 2102:0234). */
typedef enum { COK_RANDOM_PASCAL, COK_RANDOM_QUEEN } cok_random_routine;

/* How a game draws: the earlier games' 16 colours from 8x8 tiles, or The
 * Dark Queen of Krynn's VGA in 256 colours by routines of its own
 * (dark_queen.c; adventure.h's cok_drawing). */
typedef enum { COK_DRAWING_TILES, COK_DRAWING_DARK_QUEEN } cok_drawing_routine;

typedef struct cok_rules {
    /* --game's name. */
    const char *id;
    /* The assets' directory when --assets does not name one. */
    const char *assets;
    /* Why eclplay cannot play the game from a saved game, a party or
     * --play, for a game whose characters and saves the port does not
     * read yet (Dark Queen's: its title and start menu play, in krynn
     * too); NULL for one it plays. */
    const char *unplayable;
    /* How it draws (cok_drawing_routine). */
    cok_drawing_routine drawing;
    /* Its random numbers (cok_random_routine); set by cok_ecl_init, for
     * the process. */
    cok_random_routine random;
    cok_rules_text text;
    cok_rules_archives archives;
    cok_rules_files files;
    cok_rules_setup setup;
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
    cok_rules_item_record item_record;
    cok_rules_shops shops;
    cok_rules_treasure treasure;
    cok_rules_levels levels;
    cok_rules_casting casting;
    cok_rules_creation creation;
} cok_rules;

extern const cok_rules cok_rules_champions, cok_rules_death_knights, cok_rules_dark_queen;

/* The prompt WHO shows for the string operand prompt (cok_rules_ecl's
 * who_prompt): the replacement, or prompt itself. */
const char *cok_rules_who_prompt(const cok_rules *rules, const char *prompt);
/* Two strings equal with their letters' case ignored (dqk 24de). */
bool cok_rules_same_text(const char *a, const char *b);

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
