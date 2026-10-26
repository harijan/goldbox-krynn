#ifndef COK_ECL_H
#define COK_ECL_H

#include "field.h"
#include "rules.h"
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
};

typedef struct cok_ecl cok_ecl;

/* An opcode's operands (cok_rules_opcode): a header and a list over it, or
 * none ever, the original hanging. */
enum { COK_ECL_LIST = -1, COK_ECL_HANG = -2 };

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
    /* Champions' opcodes, 0x00-0x41, and the size of a game's table. */
    COK_ECL_OPCODES,
    /* What a later game's opcodes do that none of Champions' does (Death
     * Knights', dkk 3632's handlers), named by the port: the game's
     * descriptor gives each opcode its meaning (rules->ecl). */
    COK_ECL_SETUP_MONSTERS = COK_ECL_OPCODES, /* 0x0c: SETUP MONSTER with 4 operands */
    COK_ECL_PRESS_KEY,      /* 0x1d: "Press button or enter to continue" */
    COK_ECL_JOURNAL,        /* 0x1f: "This is recorded as journal entry N." */
    COK_ECL_YES_NO,         /* 0x22: a Yes/No menu; = for Yes */
    COK_ECL_CLEAR_EXIT,     /* 0x23: clear the text, redraw what changed, EXIT */
    COK_ECL_TREASURE_ITEMS, /* 0x27: coins, then a list of ITEM0's items */
    COK_ECL_NOTHING,        /* 0x29 */
    COK_ECL_MUSIC,          /* 0x2c: play a song */
    COK_ECL_ASK_MENU,       /* 0x31: HORIZONTAL MENU after "what do you do" */
    COK_ECL_STOCK,          /* 0x3c: a shop's items from ITEM0's lists */
    COK_ECL_MEANINGS
} cok_ecl_opcode;

/* What CALL's address runs (rules->ecl.calls; 2fd3:329b). */
typedef enum {
    COK_ECL_CALL_NONE,
    COK_ECL_CALL_REDRAW,  /* show again what changed */
    COK_ECL_CALL_SOUND,   /* a sound by 0x3de */
    COK_ECL_CALL_STEP,    /* a step forward */
    COK_ECL_CALL_AHEAD,   /* the wall ahead, outside 3D areas */
    COK_ECL_CALL_ANIMATE, /* the picture's next frame */
} cok_ecl_call;

/* The variables code outside the VM's operands reads and writes, by name:
 * where each lies and how wide it is are the game's (rules->ecl.vars;
 * Champions' and Death Knights' addresses in rules.c and
 * rules_death_knights.c). Champions' address is given; an array is
 * indexed from 0. */
typedef enum {
    COK_VAR_OVERLAND_X,    /* 0x4bc3: the party's square on the overland map */
    COK_VAR_OVERLAND_Y,    /* 0x4bc4 */
    COK_VAR_MAP,           /* 0x4bc5: the GEO record LOAD FILES loaded */
    COK_VAR_CLOCK,         /* 0x4bc6-0x4bcc: the clock by unit (COK_CLOCK_) */
    COK_VAR_NO_MAGIC,      /* 0x4be5: the area forbids casting */
    COK_VAR_3D,            /* 0x4be6: a 3D area (stores switch the mode) */
    COK_VAR_PIECES,        /* 0x4be7-0x4be8: LOAD PIECES loads all three unless both set */
    COK_VAR_LAST_X,        /* 0x4bf0: the party's square before its step */
    COK_VAR_LAST_Y,        /* 0x4bf1 */
    COK_VAR_BLOCK,         /* 0x4bf2: the block entered last, 0 none */
    COK_VAR_HIDDEN,        /* 0x4bfb: the area hides the party's square */
    COK_VAR_SPEED,         /* 0x4bfc: the game's speed, as saved */
    COK_VAR_SKY_LOW,       /* 0x4bfd: the sky's colour over squares below 0x80 */
    COK_VAR_SKY_HIGH,      /* 0x4bfe: and over squares from 0x80 */
    COK_VAR_PICTURES,      /* 0x4bff: pictures * 2 + animate, as saved */
    COK_VAR_LOCALS,        /* 0x4c00-0x4c1f: cleared by a block's start */
    COK_VAR_SAVINGS,       /* 0x4c33: the bank's balance, in hundreds of steel (dkk: a word over
                            * two, Champions' as loading its game copies it, 46b2:1ef0, which
                            * a new vault holds, 46b2:2e1d) */
    COK_VAR_OFF_MAP,       /* 0x4c38: a flag: outside 3D areas, off the overland map */
    COK_VAR_NO_VIEW,       /* 0x4c38 alone: a flag: no view redrawn after camp or in mode 4 */
    COK_VAR_SAVES,         /* 0x4c3c: games saved, the rule book's question every tenth */
    COK_VAR_DIFFICULTY,    /* 0x4cf4 */
    COK_VAR_BANK,          /* 0x4cf5: the bank is paying out */
    COK_VAR_OVERLAND,      /* 0x4cf7: the overland map's menu */
    COK_VAR_TREASURE_FIRST, /* 0x4cf8: the first record dropped its items */
    COK_VAR_MOONS,         /* 0x4cf9-0x4cfb: the moons' phases, 0-3 */
    COK_VAR_MOON_DAYS,     /* 0x4cfc-0x4cfe: the days of each in its phase */
    COK_VAR_TURN,          /* 0x4cff: turns the status line's facing names */
    COK_VAR_ORDER_MOON,    /* 0x4cf8 + a byte: the moon of an order of magic, 1-3 */
    COK_VAR_AREA,          /* 0x4b00-0x4bff: what a byte index reaches */
    COK_VAR_HALL,          /* 0x7ea8: the classes a training hall trains, bits */
    COK_VAR_DISTANCE_WANTED, /* 0x7ec0: the monsters' distance wanted */
    COK_VAR_DISTANCE,      /* 0x7ec1: and now */
    COK_VAR_MORALE,        /* 0x7ec6 */
    COK_VAR_OUTCOME,       /* 0x7ec7: the fight's outcome */
    COK_VAR_DROPPED,       /* 0x7ec8: records that dropped their items */
    COK_VAR_STAY,          /* 0x7ec9: 0xff: the step not taken */
    COK_VAR_SEARCH,        /* 0x7eca: bit 0 searching, bit 1 the step's events run */
    COK_VAR_SURPRISE,      /* 0x7ecb */
    COK_VAR_REST_EVERY,    /* 0x7ed2: encounters while resting: every */
    COK_VAR_REST_CHANCE,   /* 0x7ed3: and the chance */
    COK_VAR_EDGE,          /* 0x7ed5: the step left the map */
    COK_VAR_PORTRAIT,      /* 0x7ee1: the portrait's head, 0xff none */
    COK_VAR_TEMPLE,        /* 0x7ee2: open the temple */
    COK_VAR_KEEP_ITEMS,    /* 0x7ee3: the monsters keep their items */
    COK_VAR_GENTLE,        /* 0x7ee6: the party cannot kill */
    COK_VAR_ECL_FILE,      /* 0x7f12: the ECL file, as saved */
    COK_VAR_FLED,          /* 0x7f33: a member ran from the fight */
    COK_VAR_PARTY_SIZE,    /* 0x7f3e */
    COK_VAR_REMAINDER,     /* 0x7f3f: DIVIDE's remainder */
    COK_VAR_SHOP,          /* 0x7f6c: open a shop */
    COK_VAR_PRICES,        /* 0x7f6d: the shop's price factor */
    COK_VAR_HIT_BONUS,     /* 0x7f70-0x7f71: to hit, the monsters' and the party's */
    COK_VAR_MOVE_BONUS,    /* 0x7f72: the party's movement in combat */
    COK_VAR_LOCALS_GAME,   /* 0x7f79-0x7f82: cleared by a block's start */
    COK_VAR_VAULT,         /* dkk 0x4d00: COMBAT opens the vault's menu (3632:16ec); Champions none */
    COK_VAR_RIVER,         /* dkk 0x4bf3: a river crosses the open ground (410b:0a4c); Champions none */
    COK_VAR_TURNING,       /* dkk 0x4cff: turning undead's level less (5340:119b); Champions none */
    COK_VARS
} cok_var;

/* The clock's units (COK_VAR_CLOCK's elements; DS:3874's sizes). */
enum {
    COK_CLOCK_TICKS, COK_CLOCK_MINUTES, COK_CLOCK_TENS, COK_CLOCK_HOURS, COK_CLOCK_DAYS,
    COK_CLOCK_MONTHS, COK_CLOCK_YEARS, COK_CLOCK_UNITS
};

/* What a special address reads or a store to it sets (rules->ecl.reads,
 * .stores; 3775:0fca, 3775:0e06). */
typedef enum {
    COK_ECL_SPECIAL_NONE,
    COK_ECL_SPECIAL_FACING,  /* the facing, 0-6 (DS:6d87): 0x4bea, 0x33d */
    COK_ECL_SPECIAL_STEPS,   /* 0xc04d: the facing, halved in 3D areas; a store of n faces n % 4 */
    COK_ECL_SPECIAL_X,       /* 0xc04b: the party's square (DS:6d85) */
    COK_ECL_SPECIAL_Y,       /* 0xc04c */
    COK_ECL_SPECIAL_AHEAD,   /* 0xc04e: the wall ahead (DS:6d88) */
    COK_ECL_SPECIAL_SQUARE,  /* 0xc04f: the square's value (DS:6d89) */
    COK_ECL_SPECIAL_ONE,     /* reads 1 (dkk 3c1e:0d85's 0x4cf8) */
    COK_ECL_SPECIAL_B1,      /* vm->value_b1, _fb, _fc: values the host gives */
    COK_ECL_SPECIAL_FB,
    COK_ECL_SPECIAL_FC,
    COK_ECL_SPECIAL_3DE,     /* vm->value_3de, _b8, _b9: values the host takes */
    COK_ECL_SPECIAL_B8,
    COK_ECL_SPECIAL_B9,
    COK_ECL_SPECIAL_C059,    /* vm->c059, c05f: the backdrop's colours (DS:72d0, 72d1) */
    COK_ECL_SPECIAL_C05F,
} cok_ecl_special;

/* How a character field reads or is written (cok_rules_character_var). */
typedef enum {
    COK_ECL_FIELD_NONE,
    COK_ECL_FIELD_U8,       /* the field's element as a byte */
    COK_ECL_FIELD_S8,       /* as a signed byte, widened */
    COK_ECL_FIELD_U16,      /* as a word */
    COK_ECL_FIELD_VARIABLE, /* reads the variable itself */
    COK_ECL_FIELD_FILE,     /* reads the ECL file (DS:5782); a store picks the next
                             * block's (169c:0a3e): (value & 0xff) + 1 halved */
    COK_ECL_FIELD_HOST,     /* reads hooks.character_value */
    COK_ECL_FIELD_STATUS,   /* reads Champions' code for the status: 1 okay, ... */
    COK_ECL_FIELD_ACTIVE,   /* reads 1 if it can act, else 0x80 (dkk 3c1e:073a) */
    COK_ECL_FIELD_SIDE,     /* 0x80 the computer's, 0x81 hostile, else 0; a store of one
                             * of 0, 0x80, 0x81 sets both */
    COK_ECL_FIELD_ZERO,     /* reads 0, where the original leaves its result unset */
    COK_ECL_FIELD_CHARISMA, /* the reaction by charisma (3775:07d9's table) */
    COK_ECL_FIELD_CLASSES,  /* bits by class levels above 0 (rules->ecl.class_bits) */
    COK_ECL_FIELD_NAME_BYTES, /* the name's text from element index + offset: as many
                               * bytes as the field (SIZE) gives, as dkk 3c1e:073a's
                               * Move reads them */
    COK_ECL_FIELD_CLEARED,  /* a store of 0 sets vm->name_cleared */
    COK_ECL_FIELD_CLEARED_ANY, /* any store sets it */
    COK_ECL_FIELD_NPC,      /* a store above 0xb2 stores 0x32 less */
    COK_ECL_FIELD_REVIVE,   /* a store of 1 sets the status 0 and makes it able to act */
} cok_ecl_character_kind;

/* What CALL to address runs in the game rules describes. */
cok_ecl_call cok_ecl_call_of(const cok_rules *rules, uint16_t address);

/* What opcode does in the game rules describes (rules->ecl): a
 * cok_ecl_opcode, COK_ECL_MEANINGS past the game's table. */
unsigned cok_ecl_meaning_of(const cok_rules *rules, unsigned opcode);
/* What the VM's opcode (vm->opcode) does. */
unsigned cok_ecl_meaning(const cok_ecl *vm);
/* The game's handler of opcode the port does not carry out yet
 * (cok_rules_opcode's unported), or NULL. */
const char *cok_ecl_unported(const cok_rules *rules, unsigned opcode);

/* The opcode's name: Champions' from the trace table at 6d7e:0055 (NULL
 * for 0x1f), the port's for a meaning past them; NULL past the game's
 * table. */
const char *cok_ecl_opcode_name(const cok_rules *rules, unsigned opcode);

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
    COK_ECL_UNPORTED,      /* An opcode of a later game that the port does not carry
                            * out yet; the opcode hook says which. */
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
     * Champions' original also loads wall set 1, 2 or 3 (69ea:1025)
     * numbered value & 0x7f when 0x7f22, 0x7f24 or 0x7f26 gets a value
     * above 0x80 (3775:0c00; Death Knights' 3c1e:0a9a does not).
     * The VM itself sets file to (value + 1) / 2 for 0x7f12 (169c:0a3e). */
    void (*stored)(cok_ecl *vm, uint16_t address, uint16_t value, void *context);
    /* Value of a character field the VM cannot compute (kind
     * COK_ECL_FIELD_HOST): Champions' 0x7cc9, which needs 66c2:0efb, and
     * 0x7eb1 and 0x7eb4, the selected character's position in the party
     * from 0 (3775:0773; dkk 3c1e:06d4). */
    uint16_t (*character_value)(cok_ecl *vm, uint16_t address, void *context);
    /* Before each instruction, with vm->ip at its opcode. */
    void (*trace)(cok_ecl *vm, void *context);
    /* After each instruction's handler has returned without an error,
     * vm->opcode the last opcode fetched (DS:72e7, a nested run's or a
     * failed IF's skipped one too), where 2fd3:3add calls the debug trace
     * (6d7e:093e), the instruction that ends the run included. */
    void (*executed)(cok_ecl *vm, void *context);
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
    /* dkk DS:8c5b: the start menu ended the game (a Drop or Remove left no
     * party): every run stops before its next instruction, and starts none,
     * until the host starts the game over (dkk 3632:2cd5's test; 3632:2e06
     * clears it). Champions' run loop (2fd3:3add) has no such test, and
     * nothing sets it there. */
    bool halt;
    /* Comparison results, DS:72e0-72e5: =, <>, <, >, <=, >= of the first
     * operand against the second. */
    bool flags[6];

    cok_ecl_operand operand[COK_ECL_MAX_OPERANDS]; /* Original index i + 1. */
    size_t operands;
    char string[COK_ECL_MAX_STRINGS][256];         /* Slot n + 1; NUL-terminated. */
    size_t strings;
    /* VERTICAL MENU, HORIZONTAL MENU, ON GOTO and ON GOSUB (and a later
     * game's list opcodes, of a header of up to 4) load a header, then that
     * many more operands over it; header keeps the first load. */
    cok_ecl_operand header[4];
    char header_string[256]; /* VERTICAL MENU's prompt (string slot 1). */
    size_t header_operands;  /* 0 for other opcodes. */

    uint16_t calls[COK_ECL_MAX_CALLS]; /* GOSUB return addresses, DS:72d8. */
    size_t depth;

    /* Variables: 0x4b00-0x4eff in the block at DS:4b28, 0x7a00-0x7bff at
     * DS:4b30, and 0x7c00-0x7fff at DS:4b2c, one word each (one byte each
     * where the game's variable_bytes is 1, the cell's high byte 0). Code
     * outside the VM reads and writes them by name (cok_ecl_var). */
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
     * NULL reads as 0. Its fields, 0x7c00-0x7fff, lie where the game's
     * layout (rules->character) puts them. */
    uint8_t *character;
    bool missing_character;    /* DS:8855: 0x7d00 then reads 0. */
    uint8_t *saved_character;  /* DS:43bf, restored by EXIT if restore is set. */
    bool restore_character;    /* DS:43ba. */
    /* DS:43bf named a record since removed and freed (4def:3b0a), which
     * the original goes on with if it restores it; the next selection
     * kept clears it. */
    bool saved_freed;
    bool freed_selected;       /* EXIT restored that freed record. */

    cok_text_cursor cursor;    /* DS:6134/6135. */
    uint32_t seed;             /* Turbo Pascal RandSeed, DS:43ae. */
    cok_ecl_status status;
    cok_ecl_hooks hooks;
    /* The game's descriptor, from cok_ecl_init: the character record's
     * layout. */
    const cok_rules *rules;
};

/* Zero the VM and set the game's rules (never NULL) and its hooks (or
 * none). */
void cok_ecl_init(cok_ecl *vm, const cok_rules *rules, const cok_ecl_hooks *hooks);

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

/* End the running script as EXIT does (2fd3:0050), from a hook: restore
 * the selection LOAD CHARACTER changed, forget the GOSUB stack and reset
 * the text cursor. */
void cok_ecl_exit(cok_ecl *vm);

/* Decode count operands after the opcode at vm->ip and leave vm->ip on the
 * next opcode (3775:0032). Strings fill slots from the first. */
cok_ecl_status cok_ecl_operands(cok_ecl *vm, size_t count);
/* Operand i (0-based): its value (3775:0168) and its address. */
uint16_t cok_ecl_value(const cok_ecl *vm, size_t i);
uint16_t cok_ecl_address(const cok_ecl *vm, size_t i);
/* A list opcode's header operand i (0-based, below header_operands): its
 * value, as the handler read it before the list's (dkk 3632:1933). */
uint16_t cok_ecl_header_value(const cok_ecl *vm, size_t i);

/* Variable access (3775:0fca, 3775:0e06, 3775:14fc, 3775:1138). Strings
 * are stored one character per variable, NUL-terminated. A variable is a
 * word where each address holds one (Champions), else a byte (Death
 * Knights; cok_ecl_value reads an operand of type 2 and up as a word over
 * two, cok_ecl_store_to writes one). */
uint16_t cok_ecl_load_var(const cok_ecl *vm, uint16_t address);
void cok_ecl_store(cok_ecl *vm, uint16_t address, uint16_t value);
/* Store value at an operand's address (operand i, or o), as wide as its
 * type makes it where each address holds a byte (rules->ecl; dkk
 * 3c1e:0bc1); the same as cok_ecl_store where each holds a word. */
void cok_ecl_store_to(cok_ecl *vm, size_t i, uint16_t value);
void cok_ecl_store_as(cok_ecl *vm, const cok_ecl_operand *o, uint16_t value);
/* A variable by name (rules->ecl.vars), element i of an array: the value
 * its address holds, as wide as the game keeps it, as code outside the
 * VM's operands reads and writes it: without the stores' side effects (the
 * mode, the hooks, a character's field), the value's low byte where it is
 * a byte. Each asserts that the game has the variable and that i is one of
 * its elements. */
uint16_t cok_ecl_var(const cok_ecl *vm, cok_var name);
uint16_t cok_ecl_var_at(const cok_ecl *vm, cok_var name, size_t i);
void cok_ecl_set_var(cok_ecl *vm, cok_var name, uint16_t value);
void cok_ecl_set_var_at(cok_ecl *vm, cok_var name, size_t i, uint16_t value);
/* cok_ecl_set_var of the variable plus delta, wrapping as wide as it is. */
void cok_ecl_add_var(cok_ecl *vm, cok_var name, int delta);
/* Whether the flag name is set: its value is not the game's off value. */
bool cok_ecl_flag(const cok_ecl *vm, cok_var name);
/* The ECL address of element i of the game's variable name. */
uint16_t cok_ecl_var_address(const cok_rules *rules, cok_var name, size_t i);
/* Whether the game has the variable. */
bool cok_ecl_has_var(const cok_rules *rules, cok_var name);

/* What special address reads or a store to it sets in the game rules
 * describes, COK_ECL_SPECIAL_NONE for a variable or nothing. */
cok_ecl_special cok_ecl_special_read(const cok_rules *rules, uint16_t address);
cok_ecl_special cok_ecl_special_store(const cok_rules *rules, uint16_t address);

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

/* Keep the selection (DS:6096) in DS:43bf. */
static inline void cok_ecl_keep_selection(cok_ecl *vm)
{
    vm->saved_character = vm->character;
    vm->saved_freed = false;
}

/* The operand count of opcode's handler in the game rules describes,
 * COK_ECL_LIST for a header and list (see cok_ecl.header), or
 * COK_ECL_HANG, as for an opcode past the game's table. */
int cok_ecl_operand_count(const cok_rules *rules, unsigned opcode);
/* Operands a failed IF skips with opcode (3775:1e74). Champions' table
 * differs from the handlers for the list opcodes (none), ECL CLOCK, ADD NPC
 * and PROTECTION (one each). */
int cok_ecl_skip_count(const cok_rules *rules, unsigned opcode);
/* Decode the instruction at address without running it, leaving vm->ip on
 * the next one. String operands read variables as a run would. */
cok_ecl_status cok_ecl_decode(cok_ecl *vm, uint16_t address);

#endif
