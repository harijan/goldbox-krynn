#ifndef COK_ECL_H
#define COK_ECL_H

#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The ECL script interpreter from GAME.OVR: operands and variables in overlay
 * 3775, opcode handlers in overlay 2fd3. Addresses in comments are
 * segment:offset in START_FULL.EXE and DS offsets in its data segment.
 *
 * The VM runs control flow, arithmetic, comparisons and variable storage
 * itself. Opcodes that drive other parts of the game (pictures, menus,
 * combat, characters, items) are decoded and then handed to hooks->opcode. */

enum {
    COK_ECL_BASE = 0x8000,      /* Address of the first code byte. */
    COK_ECL_CODE_SIZE = 0x1e00, /* Code buffer, DS:4b34. */
    COK_ECL_MAX_OPERANDS = 64,  /* Operand arrays, DS:736e/73ae/73ee. */
    COK_ECL_MAX_STRINGS = 32,   /* String slots from DS:7430, 256 bytes each. */
    COK_ECL_MAX_CALLS = 256,    /* GOSUB depth; the original's list is unbounded. */
    COK_CHARACTER_SIZE = 0x199,
};

typedef enum {
    COK_ECL_EXIT, COK_ECL_GOTO, COK_ECL_GOSUB, COK_ECL_COMPARE, COK_ECL_ADD, COK_ECL_SUBTRACT,
    COK_ECL_DIVIDE, COK_ECL_MULTIPLY, COK_ECL_RANDOM, COK_ECL_SAVE, COK_ECL_LOAD_CHARACTER,
    COK_ECL_LOAD_MONSTER, COK_ECL_SETUP_MONSTER, COK_ECL_APPROACH, COK_ECL_PICTURE,
    COK_ECL_INPUT_NUMBER, COK_ECL_INPUT_STRING, COK_ECL_PRINT, COK_ECL_PRINTCLEAR,
    COK_ECL_RETURN, COK_ECL_COMPARE_AND, COK_ECL_VERTICAL_MENU, COK_ECL_IF_EQUAL,
    COK_ECL_IF_NOT_EQUAL, COK_ECL_IF_LESS, COK_ECL_IF_GREATER, COK_ECL_IF_LESS_EQUAL,
    COK_ECL_IF_GREATER_EQUAL, COK_ECL_CLEARMONSTERS, COK_ECL_PARTYSTRENGTH,
    COK_ECL_CHECKPARTY, COK_ECL_UNUSED_1F, COK_ECL_NEWECL, COK_ECL_LOAD_FILES,
    COK_ECL_PARTY_SURPRISE, COK_ECL_SURPRISE, COK_ECL_COMBAT, COK_ECL_ON_GOTO,
    COK_ECL_ON_GOSUB, COK_ECL_TREASURE, COK_ECL_ROB, COK_ECL_ENCOUNTER_MENU, COK_ECL_GETTABLE,
    COK_ECL_HORIZONTAL_MENU, COK_ECL_PARLAY, COK_ECL_CALL, COK_ECL_DAMAGE, COK_ECL_AND,
    COK_ECL_OR, COK_ECL_SPRITE_OFF, COK_ECL_FIND_ITEM, COK_ECL_PRINT_RETURN, COK_ECL_ECL_CLOCK,
    COK_ECL_SAVE_TABLE, COK_ECL_ADD_NPC, COK_ECL_LOAD_PIECES, COK_ECL_PROGRAM, COK_ECL_WHO,
    COK_ECL_DELAY, COK_ECL_SPELL, COK_ECL_PROTECTION, COK_ECL_CLEAR_BOX, COK_ECL_DUMP,
    COK_ECL_FIND_SPECIAL, COK_ECL_DESTROY_ITEMS, COK_ECL_ADD_EP,
    COK_ECL_OPCODES
} cok_ecl_opcode;

/* Name from the trace table at 6d7e:0055, or NULL for 0x1f and opcodes past
 * ADD EP. */
const char *cok_ecl_opcode_name(unsigned opcode);

typedef enum {
    COK_ECL_OK,
    COK_ECL_BAD_BLOCK,   /* Empty or larger than the code buffer. */
    COK_ECL_BAD_ADDRESS, /* Code read outside the code buffer. */
    COK_ECL_BAD_OPERAND, /* More operands or string operands than the arrays hold. */
    COK_ECL_BAD_OPCODE,  /* 0x1f, PROTECTION, or past ADD EP: the original hangs. */
    COK_ECL_DEEP_CALLS,  /* GOSUB nesting past COK_ECL_MAX_CALLS. */
    COK_ECL_LOAD_FAILED, /* NEWECL's load hook failed, where the original retries,
                          * or the opcode hook could not load a file. */
    COK_ECL_DIVIDE_BY_ZERO, /* The original stops with runtime error 200. */
    COK_ECL_EFFECT_FAILED, /* A spell effect needs what is not ported, or the original
                            * mishandles it; the opcode hook says why. */
    COK_ECL_UNDEFINED,     /* Outside the scripts, the original would read data it
                            * never set or write past what it owns; the caller says
                            * why. */
} cok_ecl_status;

const char *cok_ecl_status_string(cok_ecl_status status);

/* Operand types (DS:736e). A type byte and a low byte come first; types 1,
 * 2, 3 and 0x81 add a high byte, and 0x80 adds low bytes of packed text.
 * Other types are two bytes and have the value 0. */
enum {
    COK_ECL_BYTE = 0x00,    /* Immediate low byte. */
    COK_ECL_VAR = 0x01,     /* Value of the variable at high:low; 3 is the same. */
    COK_ECL_WORD = 0x02,    /* Immediate high:low, also used as an address. */
    COK_ECL_VAR_3 = 0x03,
    COK_ECL_STRING = 0x80,  /* Packed text into the next string slot. Its value
                             * reads the variable at the slot's stale high:low. */
    COK_ECL_STRVAR = 0x81,  /* String variable at high:low, into the next string slot. */
};

typedef struct {
    uint8_t type, low, high;
} cok_ecl_operand;

typedef struct cok_ecl cok_ecl;

typedef struct {
    /* Copy ECL block id into vm->code and set vm->size (3775:0361 reads
     * ECL<vm->file>.DAX and skips the record's first two bytes; see
     * cok_ecl_load). Return false on failure. */
    bool (*load)(cok_ecl *vm, uint8_t block, void *context);
    /* Carry out vm->opcode's game side, with its operands decoded and vm->ip
     * past them. Called for every opcode except GOTO, GOSUB, COMPARE, the
     * arithmetic, RANDOM, SAVE, RETURN with a GOSUB pending, COMPARE AND,
     * the IFs, ON GOTO, ON GOSUB, GETTABLE, SAVE TABLE, AND and OR. EXIT, and
     * RETURN with no GOSUB pending, arrive with vm->stop set; the original
     * then also clears its picture flags (DS:8830, 884a, 884c, 8848).
     * NEWECL arrives after the new block has started. PRINT RETURN arrives
     * after the VM moved the cursor; the original also clears DS:8849. */
    void (*opcode)(cok_ecl *vm, void *context);
    /* After every variable store, once the VM has updated its own state.
     * The original also loads wall set 1, 2 or 3 (69ea:1025) numbered
     * value & 0x7f when 0x7f22, 0x7f24 or 0x7f26 gets a value above 0x80.
     * The VM itself sets file to (value + 1) / 2 for 0x7f12 (169c:0a3e). */
    void (*stored)(cok_ecl *vm, uint16_t address, uint16_t value, void *context);
    /* Value of a character field the VM cannot compute: 0x7cc9, which needs
     * 66c2:0efb, and 0x7eb1 and 0x7eb4, the selected character's position
     * in the party from 0 (3775:0773). */
    uint16_t (*character_value)(cok_ecl *vm, uint16_t address, void *context);
    /* Before each instruction, with vm->ip at its opcode. */
    void (*trace)(cok_ecl *vm, void *context);
    void *context;
} cok_ecl_hooks;

struct cok_ecl {
    uint8_t code[COK_ECL_CODE_SIZE]; /* Addresses 0x8000-0x9dff; scripts write here. */
    size_t size;                     /* Loaded bytes. */
    uint8_t block;                   /* DS:8846. */
    uint8_t file;                    /* ECL file number, DS:5782; see 0x7f12. */
    uint8_t previous_file;           /* DS:5783. */
    uint16_t ip;                     /* DS:4b43. */
    /* DS:4b39, 4b3b, 4b3d, 4b3f, 4b41: after a move, location events, camp,
     * after resting, and on load (see 2fd3:3c28, 2fd3:3403). */
    uint16_t vectors[5];
    uint8_t opcode, previous;        /* DS:72e7, DS:72e6. */
    bool stop;                       /* DS:43b8, set by EXIT. */
    bool reload;                     /* DS:43b9, set by NEWECL. */
    bool abort;                      /* DS:4b57; set by the host to end a run. */
    /* Comparison results, DS:72e0-72e5: =, <>, <, >, <=, >= of the first
     * operand against the second. */
    bool flags[6];

    cok_ecl_operand operand[COK_ECL_MAX_OPERANDS]; /* Original index i + 1. */
    size_t operands;
    char string[COK_ECL_MAX_STRINGS][256];         /* Slot n + 1; NUL-terminated. */
    size_t strings;
    /* VERTICAL MENU, HORIZONTAL MENU, ON GOTO and ON GOSUB load a header,
     * then that many more operands over it; header keeps the first load. */
    cok_ecl_operand header[3];
    char header_string[256]; /* VERTICAL MENU's prompt (string slot 1). */
    size_t header_operands;  /* 0 for other opcodes. */

    uint16_t calls[COK_ECL_MAX_CALLS]; /* GOSUB return addresses, DS:72d8. */
    size_t depth;

    /* Variables: 0x4b00-0x4eff in the block at DS:4b28, 0x7a00-0x7bff at
     * DS:4b30, and 0x7c00-0x7fff at DS:4b2c, one word each. */
    uint16_t mem4b00[0x400];
    uint16_t mem7a00[0x200];
    uint16_t mem7c00[0x400];

    /* Game state read and written through special addresses. */
    int8_t map_x, map_y;      /* DS:6d85, 6d86 (0xc04b, 0xc04c). */
    uint8_t direction;        /* DS:6d87: 0, 2, 4 or 6 (0xc04d, 0x4bea, 0x033d). */
    uint8_t ahead, square;    /* DS:6d88, 6d89 (0xc04e, 0xc04f). */
    uint8_t c059, c05f;       /* DS:72d0, 72d1. */
    uint16_t value_fb, value_fc, value_b1;   /* DS:72d2, 72d4, 72d6, read-only. */
    uint16_t value_3de, value_b8, value_b9;  /* DS:8834, 8836, 8838, write-only. */
    uint8_t mode, last_mode;  /* DS:4b49, 4b4a; a store to 0x4be6 switches 3/4. */
    bool view_changed;        /* DS:8850. */
    bool c059_changed;        /* DS:884f. */
    bool area_changed;        /* DS:8852, stores to 0x4bfd or 0x4bfe. */
    bool name_cleared;        /* DS:883b, store of 0 to 0x7c00. */
    bool keep_vars;           /* DS:4b52: NEWECL keeps 0x4c00-0x4c1f and 0x7f79-0x7f82. */

    /* The selected character's record (DS:6096), maintained by the host;
     * NULL reads as 0. */
    uint8_t *character;
    bool missing_character;    /* DS:8855: 0x7d00 then reads 0. */
    uint8_t *saved_character;  /* DS:43bf, restored by EXIT if restore is set. */
    bool restore_character;    /* DS:43ba. */

    cok_text_cursor cursor;    /* DS:6134/6135. */
    uint32_t seed;             /* Turbo Pascal RandSeed, DS:43ae. */
    cok_ecl_status status;
    cok_ecl_hooks hooks;
};

/* Zero the VM and set its hooks. */
void cok_ecl_init(cok_ecl *vm, const cok_ecl_hooks *hooks);

/* Copy a decoded ECL DAX record into the code buffer, skipping its first
 * two bytes and zeroing the rest (3775:0361). */
cok_ecl_status cok_ecl_load(cok_ecl *vm, const uint8_t *record, size_t size);

/* Reset the VM for the loaded block and read its five vectors (3775:01e8).
 * reset_vars clears 0x4c00-0x4c1f and 0x7f79-0x7f82, as the original does
 * unless DS:4b52 is set. The original also resets its picture flags and
 * monster count (DS:884a, 884c, 883a, 8830, 72eb, 742e); hosts reset their
 * own state when they load a block. */
cok_ecl_status cok_ecl_start(cok_ecl *vm, bool reset_vars);

/* Run from address until EXIT, vm->abort, or an error (2fd3:3add). Runs may
 * nest, from hooks; the caller saves vm->ip if it must resume. */
cok_ecl_status cok_ecl_run(cok_ecl *vm, uint16_t address);

/* Decode count operands after the opcode at vm->ip and leave vm->ip on the
 * next opcode (3775:0032). Strings fill slots from the first. */
cok_ecl_status cok_ecl_operands(cok_ecl *vm, size_t count);
/* Operand i (0-based): its value (3775:0168) and its address. */
uint16_t cok_ecl_value(const cok_ecl *vm, size_t i);
uint16_t cok_ecl_address(const cok_ecl *vm, size_t i);

/* Variable access (3775:0fca, 3775:0e06, 3775:14fc, 3775:1138). Strings
 * are stored one character per variable, NUL-terminated. */
uint16_t cok_ecl_load_var(const cok_ecl *vm, uint16_t address);
void cok_ecl_store(cok_ecl *vm, uint16_t address, uint16_t value);
void cok_ecl_load_string(const cok_ecl *vm, uint16_t address, char out[256]);
void cok_ecl_store_string(cok_ecl *vm, uint16_t address, const char *text);

/* Turbo Pascal's Random(range): advance the seed, return its high word
 * modulo range, or 0 for range 0 (1a46:1179). */
uint16_t cok_tp_random(uint32_t *seed, uint16_t range);

/* The game's dice (60f4:1216): the sum, as a byte, of count rolls of
 * Random(sides) + 1. Nearly every roll in the game goes through it. */
uint8_t cok_dice(uint32_t *seed, uint8_t count, uint8_t sides);
/* 60f4:1261: cok_dice, after storing count in *dice (DS:6b34, the dice of
 * the damage being dealt). */
uint8_t cok_dice_count(uint32_t *seed, uint8_t count, uint8_t sides, uint8_t *dice);

/* The operand count of opcode's handler, COK_ECL_LIST for a header and
 * list (see cok_ecl.header), or COK_ECL_HANG. */
enum { COK_ECL_LIST = -1, COK_ECL_HANG = -2 };
int cok_ecl_operand_count(unsigned opcode);
/* Operands a failed IF skips with opcode (3775:1e74). Its table differs from
 * the handlers for the list opcodes (none), ECL CLOCK, ADD NPC and
 * PROTECTION (one each). */
int cok_ecl_skip_count(unsigned opcode);
/* Decode the instruction at address without running it, leaving vm->ip on
 * the next one. String operands read variables as a run would. */
cok_ecl_status cok_ecl_decode(cok_ecl *vm, uint16_t address);

#endif
