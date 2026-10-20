#include "ecl.h"

#include <assert.h>
#include <string.h>

/* By meaning: Champions' trace table at 6d7e:0055 (NULL for 0x1f), then
 * the port's names for a later game's meanings. */
static const char *const names[COK_ECL_MEANINGS] = {
    "EXIT", "GOTO", "GOSUB", "COMPARE", "ADD", "SUBTRAT", "DIVIDE", "MULTIPLY", "RANDOM", "SAVE",
    "LOAD CHARACTER", "LOAD MONSTER", "SETUP MONSTER", "APPROACH", "PICTURE", "INPUT NUMBER",
    "INPUT STRING", "PRINT", "PRINTCLEAR", "RETURN", "COMPARE AND", "VERTICAL MENU", "IF =",
    "IF <>", "IF <", "IF >", "IF <=", "IF >=", "CLEARMONSTERS", "PARTYSTRENGTH", "CHECKPARTY",
    NULL, "NEWECL", "LOAD FILES", "PARTY SURPRISE", "SURPRISE", "COMBAT", "ON GOTO", "ON GOSUB",
    "TREASURE", "ROB", "ENCOUNTER MENU", "GETTABLE", "HORIZONTAL MENU", "PARLAY", "CALL",
    "DAMAGE", "AND", "OR", "SPRITE OFF", "FIND ITEM", "PRINT RETURN", "ECL CLOCK", "SAVE TABLE",
    "ADD NPC", "LOAD PIECES", "PROGRAM", "WHO", "DELAY", "SPELL", "PROTECTION", "CLEAR BOX",
    "DUMP", "FIND SPECIAL", "DESTROY ITEMS", "ADD EP",
    [COK_ECL_SETUP_MONSTERS] = "SETUP MONSTERS", [COK_ECL_PRESS_KEY] = "PRESS KEY",
    [COK_ECL_JOURNAL] = "JOURNAL", [COK_ECL_YES_NO] = "YES NO",
    [COK_ECL_CLEAR_EXIT] = "CLEAR EXIT", [COK_ECL_TREASURE_ITEMS] = "TREASURE ITEMS",
    [COK_ECL_NOTHING] = "NOTHING", [COK_ECL_MUSIC] = "MUSIC", [COK_ECL_ASK_MENU] = "ASK MENU",
    [COK_ECL_STOCK] = "STOCK",
};

/* The game's table entry for opcode, or NULL past its table. */
static const cok_rules_opcode *instruction(const cok_rules *rules, unsigned opcode)
{
    assert(rules != NULL && rules->ecl.opcodes != NULL);
    return opcode < rules->ecl.count ? &rules->ecl.opcodes[opcode] : NULL;
}

unsigned cok_ecl_meaning_of(const cok_rules *rules, unsigned opcode)
{
    const cok_rules_opcode *op = instruction(rules, opcode);
    return op != NULL ? op->meaning : COK_ECL_MEANINGS;
}

cok_ecl_call cok_ecl_call_of(const cok_rules *rules, uint16_t address)
{
    for (size_t i = 0; i < sizeof rules->ecl.calls / sizeof *rules->ecl.calls; ++i)
        if (rules->ecl.calls[i].routine != COK_ECL_CALL_NONE &&
            rules->ecl.calls[i].address == address)
            return (cok_ecl_call)rules->ecl.calls[i].routine;
    return COK_ECL_CALL_NONE;
}

const char *cok_ecl_unported(const cok_rules *rules, unsigned opcode)
{
    const cok_rules_opcode *op = instruction(rules, opcode);
    return op != NULL ? op->unported : NULL;
}

unsigned cok_ecl_meaning(const cok_ecl *vm)
{
    return cok_ecl_meaning_of(vm->rules, vm->opcode);
}

const char *cok_ecl_opcode_name(const cok_rules *rules, unsigned opcode)
{
    unsigned meaning = cok_ecl_meaning_of(rules, opcode);
    return meaning < COK_ECL_MEANINGS ? names[meaning] : NULL;
}

const char *cok_ecl_status_string(cok_ecl_status status)
{
    switch (status) {
    case COK_ECL_OK: return "ok";
    case COK_ECL_BAD_BLOCK: return "ECL block is empty or too large";
    case COK_ECL_BAD_ADDRESS: return "code address outside the ECL buffer";
    case COK_ECL_BAD_OPERAND: return "too many operands or strings";
    case COK_ECL_BAD_OPCODE: return "opcode that never advances";
    case COK_ECL_DEEP_CALLS: return "GOSUB nesting too deep";
    case COK_ECL_LOAD_FAILED: return "ECL block failed to load";
    case COK_ECL_DIVIDE_BY_ZERO: return "division by zero";
    case COK_ECL_EFFECT_FAILED: return "spell effect not carried out";
    case COK_ECL_UNDEFINED: return "the original's behaviour is undefined";
    case COK_ECL_UNPORTED: return "an opcode not ported yet";
    }
    return "unknown status";
}

int cok_ecl_skip_count(const cok_rules *rules, unsigned opcode)
{
    const cok_rules_opcode *op = instruction(rules, opcode);
    return op != NULL ? op->skip : 0;
}

int cok_ecl_operand_count(const cok_rules *rules, unsigned opcode)
{
    const cok_rules_opcode *op = instruction(rules, opcode);
    return op != NULL ? op->operands : COK_ECL_HANG;
}

/* The operands of a list opcode's header, the last of which counts the
 * list. */
static size_t header_count(const cok_ecl *vm)
{
    const cok_rules_opcode *op = instruction(vm->rules, vm->opcode);
    return op != NULL ? op->header : 0;
}

uint16_t cok_tp_random(uint32_t *seed, uint16_t range)
{
    *seed = *seed * 0x08088405u + 1;
    if (range == 0)
        return 0;
    return (uint16_t)((*seed >> 16) % range);
}

uint8_t cok_dice(uint32_t *seed, uint8_t count, uint8_t sides)
{
    uint8_t sum = 0;
    for (unsigned i = 0; i < count; ++i) sum = (uint8_t)(sum + cok_tp_random(seed, sides) + 1);
    return sum;
}

uint8_t cok_dice_count(uint32_t *seed, uint8_t count, uint8_t sides, uint8_t *dice)
{
    *dice = count;
    return cok_dice(seed, count, sides);
}

void cok_ecl_init(cok_ecl *vm, const cok_rules *rules, const cok_ecl_hooks *hooks)
{
    assert(rules != NULL);
    memset(vm, 0, sizeof *vm);
    vm->rules = rules;
    if (hooks != NULL) vm->hooks = *hooks;
}

cok_ecl_status cok_ecl_load(cok_ecl *vm, const uint8_t *record, size_t size)
{
    if (size < 2 || size - 2 > COK_ECL_CODE_SIZE) return COK_ECL_BAD_BLOCK;
    memset(vm->code, 0, sizeof vm->code);
    memcpy(vm->code, record + 2, size - 2);
    vm->size = size - 2;
    return COK_ECL_OK;
}

/* Variables. */

enum { AREA, GAME, SCRATCH, CODE, SPECIAL };

/* Which store an address belongs to (3775:0718). */
static int area_of(uint16_t address)
{
    if (address >= 0x4b00 && address < 0x4f00) return AREA;
    if (address >= 0x7c00 && address < 0x8000) return GAME;
    if (address >= 0x7a00 && address < 0x7c00) return SCRATCH;
    if (address >= 0x8000 && address < 0x9e00) return CODE;
    return SPECIAL;
}

/* The word for a variable address in a store, or NULL past its end. Like
 * strchr, it takes a const VM and returns a writable pointer. */
static uint16_t *word(const cok_ecl *vm, int area, uint32_t address)
{
    const uint16_t *w = NULL;
    if (area == AREA && address - 0x4b00u < 0x400) w = &vm->mem4b00[address - 0x4b00];
    if (area == GAME && address - 0x7c00u < 0x400) w = &vm->mem7c00[address - 0x7c00];
    if (area == SCRATCH && address - 0x7a00u < 0x200) w = &vm->mem7a00[address - 0x7a00];
    return (uint16_t *)w;
}

static uint8_t *code_byte(const cok_ecl *vm, uint32_t address)
{
    return address - COK_ECL_BASE < COK_ECL_CODE_SIZE
               ? (uint8_t *)&vm->code[address - COK_ECL_BASE]
               : NULL;
}

/* Whether each variable address holds a byte (rules->ecl.variable_bytes),
 * so that a wide access reads or writes a word over two addresses (dkk
 * 3c1e:0d85, 3c1e:0bc1); where each holds a word, as in Champions, the
 * width is the word's whatever the access. */
static bool byte_variables(const cok_ecl *vm)
{
    assert(vm->rules != NULL);
    return vm->rules->ecl.variable_bytes == 1;
}

/* The byte after address in its store, for a word over two addresses;
 * past the store's end 0 (the original reads on into what follows it). */
static uint8_t next_byte(const cok_ecl *vm, uint16_t address)
{
    uint32_t next = (uint32_t)address + 1;
    int area = area_of(address);
    if (area == CODE) {
        const uint8_t *b = code_byte(vm, next);
        return b != NULL ? *b : 0;
    }
    const uint16_t *w = word(vm, area, next);
    return w != NULL ? (uint8_t)*w : 0;
}

/* Put a word's high byte after address in its store. */
static void store_high(cok_ecl *vm, uint16_t address, uint16_t value)
{
    uint32_t next = (uint32_t)address + 1;
    int area = area_of(address);
    uint8_t *b;
    uint16_t *w;
    if (area == CODE && (b = code_byte(vm, next)) != NULL)
        *b = (uint8_t)(value >> 8);
    else if (area != CODE && (w = word(vm, area, next)) != NULL)
        *w = (uint8_t)(value >> 8);
}

/* Variables by name. */

static const cok_rules_var *var_entry(const cok_rules *rules, cok_var name, size_t i)
{
    assert(rules != NULL && rules->ecl.vars != NULL && name < COK_VARS);
    const cok_rules_var *v = &rules->ecl.vars[name];
    assert(v->address != 0 && "the game has no such variable");
    assert(i < v->count && "past the variable's elements");
    assert(v->width >= rules->ecl.variable_bytes);
    return v;
}

bool cok_ecl_has_var(const cok_rules *rules, cok_var name)
{
    assert(rules != NULL && rules->ecl.vars != NULL && name < COK_VARS);
    return rules->ecl.vars[name].address != 0;
}

uint16_t cok_ecl_var_address(const cok_rules *rules, cok_var name, size_t i)
{
    const cok_rules_var *v = var_entry(rules, name, i);
    return (uint16_t)(v->address + i * v->width / rules->ecl.variable_bytes);
}

/* The cell of element i of name, which lies in one of the variables'
 * stores. */
static uint16_t *var_word(const cok_ecl *vm, cok_var name, size_t i, uint16_t *address)
{
    *address = cok_ecl_var_address(vm->rules, name, i);
    uint16_t *w = word(vm, area_of(*address), *address);
    assert(w != NULL && "a variable outside the variables' stores");
    return w;
}

uint16_t cok_ecl_var_at(const cok_ecl *vm, cok_var name, size_t i)
{
    uint16_t address;
    const uint16_t *w = var_word(vm, name, i, &address);
    if (!byte_variables(vm)) return *w;
    uint16_t value = (uint8_t)*w;
    if (vm->rules->ecl.vars[name].width == 2)
        value = (uint16_t)(value | next_byte(vm, address) << 8);
    return value;
}

uint16_t cok_ecl_var(const cok_ecl *vm, cok_var name)
{
    return cok_ecl_var_at(vm, name, 0);
}

void cok_ecl_set_var_at(cok_ecl *vm, cok_var name, size_t i, uint16_t value)
{
    uint16_t address;
    uint16_t *w = var_word(vm, name, i, &address);
    if (!byte_variables(vm)) {
        *w = value;
        return;
    }
    *w = (uint8_t)value;
    if (vm->rules->ecl.vars[name].width == 2) store_high(vm, address, value);
}

void cok_ecl_set_var(cok_ecl *vm, cok_var name, uint16_t value)
{
    cok_ecl_set_var_at(vm, name, 0, value);
}

void cok_ecl_add_var(cok_ecl *vm, cok_var name, int delta)
{
    cok_ecl_set_var(vm, name, (uint16_t)(cok_ecl_var(vm, name) + delta));
}

bool cok_ecl_flag(const cok_ecl *vm, cok_var name)
{
    return cok_ecl_var(vm, name) != vm->rules->ecl.vars[name].off;
}

/* Special addresses. */

static cok_ecl_special special_in(const cok_rules_special *list, unsigned count, uint16_t address)
{
    for (unsigned i = 0; i < count; ++i)
        if (list[i].address == address) return (cok_ecl_special)list[i].what;
    return COK_ECL_SPECIAL_NONE;
}

cok_ecl_special cok_ecl_special_read(const cok_rules *rules, uint16_t address)
{
    assert(rules != NULL);
    return special_in(rules->ecl.reads, rules->ecl.read_count, address);
}

cok_ecl_special cok_ecl_special_store(const cok_rules *rules, uint16_t address)
{
    assert(rules != NULL);
    return special_in(rules->ecl.stores, rules->ecl.store_count, address);
}

/* What a special address reads (3775:0fca's, dkk 3c1e:0d85's). */
static uint16_t special_value(const cok_ecl *vm, cok_ecl_special what)
{
    switch (what) {
    case COK_ECL_SPECIAL_FACING: return vm->direction;
    case COK_ECL_SPECIAL_STEPS:
        return cok_ecl_var(vm, COK_VAR_3D) == 0 ? vm->direction : vm->direction >> 1;
    case COK_ECL_SPECIAL_X: return (uint16_t)(int16_t)vm->map_x;
    case COK_ECL_SPECIAL_Y: return (uint16_t)(int16_t)vm->map_y;
    case COK_ECL_SPECIAL_AHEAD: return vm->ahead;
    case COK_ECL_SPECIAL_SQUARE: return vm->square;
    case COK_ECL_SPECIAL_ONE: return 1;
    case COK_ECL_SPECIAL_B1: return vm->value_b1;
    case COK_ECL_SPECIAL_FB: return vm->value_fb;
    case COK_ECL_SPECIAL_FC: return vm->value_fc;
    default: return 0;
    }
}

/* What a store to a special address sets (3775:0e06's, dkk 3c1e:0bc1's). */
static void special_store(cok_ecl *vm, cok_ecl_special what, uint16_t value)
{
    switch (what) {
    case COK_ECL_SPECIAL_3DE: vm->value_3de = value; break;
    case COK_ECL_SPECIAL_B8: vm->value_b8 = value; break;
    case COK_ECL_SPECIAL_B9: vm->value_b9 = value; break;
    case COK_ECL_SPECIAL_X: vm->map_x = (int8_t)value; vm->view_changed = true; break;
    case COK_ECL_SPECIAL_Y: vm->map_y = (int8_t)value; vm->view_changed = true; break;
    case COK_ECL_SPECIAL_STEPS:
        vm->direction = (uint8_t)(value % 4 * 2);
        vm->view_changed = true;
        break;
    case COK_ECL_SPECIAL_C059: vm->c059 = (uint8_t)value; vm->c059_changed = true; break;
    case COK_ECL_SPECIAL_C05F: vm->c05f = (uint8_t)value; vm->c059_changed = true; break;
    default: break;
    }
}

/* The selected character's fields. */

/* The selected character's layout, the game's: a VM made without
 * cok_ecl_init's rules stops here, at its first character field, not
 * reading through NULL. */
static const cok_character_layout *layout(const cok_ecl *vm)
{
    assert(vm->rules != NULL);
    return vm->rules->character;
}

/* The game's entry for field (an offset from 0x7c00) read or written, or
 * NULL: the variable itself. */
static const cok_rules_character_var *character_var(const cok_ecl *vm, unsigned field, bool write)
{
    assert(vm->rules != NULL);
    const cok_rules_ecl *E = &vm->rules->ecl;
    for (unsigned i = 0; i < E->character_count; ++i) {
        const cok_rules_character_var *e = &E->character[i];
        if (field >= e->first && field <= e->last &&
            (write ? e->write : e->read) != COK_ECL_FIELD_NONE)
            return e;
    }
    return NULL;
}

/* The element of the entry's field that field reads or writes. */
static long element(const cok_rules_character_var *e, unsigned field)
{
    return (long)e->index + (long)(field - e->first) / (e->step != 0 ? e->step : 1);
}

/* Character fields read through 0x7c00-0x7fff (3775:07d9, dkk 3c1e:073a);
 * *found is false for addresses that read the variable itself. */
static uint16_t character_field(const cok_ecl *vm, uint16_t address, bool *found)
{
    unsigned field = address - 0x7c00u;
    const cok_rules_character_var *e = character_var(vm, field, false);
    *found = e != NULL;
    if (e == NULL) return 0;
    switch (e->read) {
    case COK_ECL_FIELD_VARIABLE: return vm->mem7c00[field];
    case COK_ECL_FIELD_FILE: return vm->file;
    case COK_ECL_FIELD_HOST:
        if (vm->hooks.character_value == NULL) return 0;
        return vm->hooks.character_value((cok_ecl *)vm, address, vm->hooks.context);
    /* The original leaves the result unset (Champions' 0x10d); 0 here. */
    case COK_ECL_FIELD_ZERO: return 0;
    default: break;
    }
    const uint8_t *c = vm->character;
    if (c == NULL) return 0;
    const cok_character_layout *L = layout(vm);
    cok_field f = (cok_field)e->field;
    long k = element(e, field);
    /* A field of the spells' block reads 0 from a record that holds none
     * (dkk 3c1e:073a tests its pointer). */
    if (cok_field_in_spells(L, f) && !cok_field_has_spells(L, c)) return 0;
    switch (e->read) {
    case COK_ECL_FIELD_U8: return cok_field_u8_at(L, c, f, k);
    case COK_ECL_FIELD_S8: return (uint16_t)(int16_t)cok_field_s8_at(L, c, f, k);
    case COK_ECL_FIELD_U16: return cok_field_u16_at(L, c, f, k);
    case COK_ECL_FIELD_STATUS:
        if (vm->missing_character) return 0;
        switch (cok_field_u8(L, c, COK_FIELD_STATUS)) {
        case 0: return 1;
        case 3: return 6;
        case 4: return 5;
        case 5: return 4;
        case 6: return 3;
        case 7: return 7;
        case 8: return 2;
        default: return 0;
        }
    case COK_ECL_FIELD_ACTIVE: return cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0 ? 0x80 : 1;
    case COK_ECL_FIELD_SIDE: {
        uint8_t hostile = cok_field_u8(L, c, COK_FIELD_HOSTILE);
        if (hostile == 0 && cok_field_u8(L, c, COK_FIELD_COMPUTER) != 0) return 0x80;
        return hostile == 1 ? 0x81 : 0;
    }
    case COK_ECL_FIELD_CHARISMA: {
        /* The original leaves the result unset for unlisted values; 0
         * here. */
        static const uint8_t table[26] = {0,  0,  0,  0,  5,  10, 15, 20, 25, 25, 25, 25, 25,
                                          30, 35, 40, 50, 55, 60, 60, 60, 60, 60, 60, 60, 60};
        uint8_t charisma = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_CHARISMA);
        return charisma >= sizeof table ? 0 : table[charisma];
    }
    case COK_ECL_FIELD_CLASSES: {
        uint16_t bits = 0;
        for (unsigned b = 0; b < 8; ++b) {
            uint8_t class_ = vm->rules->ecl.class_bits[b];
            if (class_ != 0xff && cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class_) > 0)
                bits = (uint16_t)(bits | 1u << b);
        }
        return bits;
    }
    case COK_ECL_FIELD_NAME_BYTES: {
        /* Move(name + 1 + offset, result, size): a count of the SIZE
         * byte, 1 for a man; 2 and up read a word (past 4 the original
         * writes on past its result). */
        size_t at = (size_t)cok_field_offset_unchecked(L, f, 0, k);
        uint8_t count = cok_field_u8(L, c, COK_FIELD_SIZE);
        if (count == 0) return 0;
        if (count == 1) return c[at];
        return (uint16_t)(c[at] | c[at + 1] << 8);
    }
    default: return 0;
    }
}

uint16_t cok_ecl_load_var(const cok_ecl *vm, uint16_t address)
{
    cok_ecl_special what = cok_ecl_special_read(vm->rules, address);
    if (what != COK_ECL_SPECIAL_NONE) return special_value(vm, what);
    switch (area_of(address)) {
    case AREA: return vm->mem4b00[address - 0x4b00];
    case GAME: {
        bool found;
        uint16_t value = character_field(vm, address, &found);
        return found ? value : vm->mem7c00[address - 0x7c00];
    }
    case SCRATCH: return vm->mem7a00[address - 0x7a00];
    case CODE: return vm->code[address - COK_ECL_BASE];
    /* Unlisted special addresses leave the original's result unset; 0
     * here. */
    default: return 0;
    }
}

/* Character fields written through 0x7c00-0x7fff (3775:0c00, dkk
 * 3c1e:0a9a). */
static void store_character(cok_ecl *vm, uint16_t address, uint16_t value)
{
    unsigned field = address - 0x7c00u;
    const cok_rules_character_var *e = character_var(vm, field, true);
    if (e == NULL) return;
    switch (e->write) {
    case COK_ECL_FIELD_CLEARED:
        if (value == 0) vm->name_cleared = true;
        return;
    case COK_ECL_FIELD_CLEARED_ANY: vm->name_cleared = true; return;
    case COK_ECL_FIELD_FILE:
        /* 169c:0a3e: pick the ECL file holding the next block. */
        vm->previous_file = vm->file;
        vm->file = (uint8_t)(((value & 0xff) + 1) >> 1);
        return;
    default: break;
    }
    uint8_t *c = vm->character;
    if (c == NULL) return;
    const cok_character_layout *L = layout(vm);
    cok_field f = (cok_field)e->field;
    long k = element(e, field);
    /* And takes no write there (dkk 3c1e:0a9a). */
    if (cok_field_in_spells(L, f) && !cok_field_has_spells(L, c)) return;
    switch (e->write) {
    case COK_ECL_FIELD_U8: cok_field_set8_at(L, c, f, k, (uint8_t)value); break;
    case COK_ECL_FIELD_U16: cok_field_set16_at(L, c, f, k, value); break;
    case COK_ECL_FIELD_NPC:
        cok_field_set8_at(L, c, f, k, (uint8_t)(value > 0xb2 ? value - 0x32 : value));
        break;
    case COK_ECL_FIELD_REVIVE:
        if (value == 1) {
            cok_field_set8(L, c, COK_FIELD_STATUS, 0);
            cok_field_set8(L, c, COK_FIELD_ACTIVE, 1);
        }
        break;
    case COK_ECL_FIELD_SIDE:
        if (value == 0 || value == 0x80 || value == 0x81) {
            cok_field_set8(L, c, COK_FIELD_HOSTILE, value == 0x81);
            cok_field_set8(L, c, COK_FIELD_COMPUTER, value != 0);
        }
        break;
    default: break;
    }
}

/* An operand of type 2 and up is read and written as a word where each
 * address holds a byte (dkk 3c1e:0d85's and 0bc1's width, 1 for a type
 * below 2). */
static bool wide_type(uint8_t type)
{
    return type >= COK_ECL_WORD;
}

/* A variable read as wide or not: where each address holds a byte, a wide
 * read of a store's variable (not a special address, such as the facing
 * at 0x4bea, or a character field) takes the next byte as its high one. */
static uint16_t load_sized(const cok_ecl *vm, uint16_t address, bool wide)
{
    uint16_t value = cok_ecl_load_var(vm, address);
    if (!wide || !byte_variables(vm)) return value;
    int area = area_of(address);
    if (area == SPECIAL || cok_ecl_special_read(vm->rules, address) != COK_ECL_SPECIAL_NONE)
        return value;
    if (area == GAME) {
        bool found;
        character_field(vm, address, &found);
        if (found) return value;
    }
    return (uint16_t)((value & 0xff) | next_byte(vm, address) << 8);
}

/* A store, wide or not: where each address holds a byte, a store that is
 * not wide keeps the value's low byte, and a wide one into a store puts
 * its high byte in the next address too (dkk 3c1e:0bc1). */
static void store_sized(cok_ecl *vm, uint16_t address, uint16_t value, bool wide)
{
    bool bytes = byte_variables(vm);
    if (bytes && !wide) value &= 0xff;
    bool high = bytes && wide;
    switch (area_of(address)) {
    case AREA:
        /* The same in both games (3775:0e06, dkk 3c1e:0bc1). */
        if (address == 0x4bfd || address == 0x4bfe) {
            vm->area_changed = true;
        } else if (address == 0x4be6 && vm->mem4b00[0xe6] != value) {
            vm->last_mode = vm->mode;
            vm->mode = value == 0 ? 3 : 4;
        }
        vm->mem4b00[address - 0x4b00] = high ? value & 0xff : value;
        if (high) store_high(vm, address, value);
        break;
    case GAME:
        vm->mem7c00[address - 0x7c00] = high ? value & 0xff : value;
        if (high) store_high(vm, address, value);
        store_character(vm, address, value);
        break;
    case SCRATCH:
        vm->mem7a00[address - 0x7a00] = high ? value & 0xff : value;
        if (high) store_high(vm, address, value);
        break;
    case CODE:
        vm->code[address - COK_ECL_BASE] = (uint8_t)value;
        if (high) store_high(vm, address, value);
        break;
    default: special_store(vm, cok_ecl_special_store(vm->rules, address), value); break;
    }
    if (vm->hooks.stored != NULL) vm->hooks.stored(vm, address, value, vm->hooks.context);
}

void cok_ecl_store(cok_ecl *vm, uint16_t address, uint16_t value)
{
    store_sized(vm, address, value, false);
}

void cok_ecl_store_as(cok_ecl *vm, const cok_ecl_operand *o, uint16_t value)
{
    store_sized(vm, (uint16_t)(o->high << 8 | o->low), value, wide_type(o->type));
}

void cok_ecl_store_to(cok_ecl *vm, size_t i, uint16_t value)
{
    cok_ecl_store_as(vm, &vm->operand[i], value);
}

void cok_ecl_load_string(const cok_ecl *vm, uint16_t address, char out[256])
{
    int area = area_of(address);
    unsigned length = 0;
    if (address == 0x7c00) {
        if (vm->character != NULL) {
            const cok_character_layout *L = layout(vm);
            uint8_t stored = cok_field_u8_at(L, vm->character, COK_FIELD_NAME, 0);
            length = stored > 15 ? 15 : stored;
            memcpy(out, vm->character + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
        }
    } else if (area == CODE) {
        const uint8_t *b;
        while (length < 255 && (b = code_byte(vm, address + length)) != NULL && *b != 0)
            out[length++] = (char)*b;
    } else if (area != SPECIAL) {
        const uint16_t *w;
        while (length < 255 && (w = word(vm, area, address + length)) != NULL && *w != 0)
            out[length++] = (char)*w;
    }
    out[length] = '\0';
}

void cok_ecl_store_string(cok_ecl *vm, uint16_t address, const char *text)
{
    unsigned length = 0;
    while (length < 255 && text[length] != '\0') ++length;
    int area = area_of(address);
    if (address == 0x7c00) {
        if (vm->character != NULL) {
            const cok_character_layout *L = layout(vm);
            unsigned n = length > 15 ? 15 : length;
            cok_field_set8_at(L, vm->character, COK_FIELD_NAME, 0, (uint8_t)n);
            memcpy(vm->character + cok_field_offset_at(L, COK_FIELD_NAME, 1), text, n);
        }
        return;
    }
    for (unsigned i = 0; i <= length; ++i) {
        uint8_t c = i < length ? (uint8_t)text[i] : 0;
        uint8_t *b;
        uint16_t *w;
        if (area == CODE && (b = code_byte(vm, address + i)) != NULL)
            *b = c;
        else if (area != CODE && area != SPECIAL && (w = word(vm, area, address + i)) != NULL)
            *w = c;
    }
}

/* Operands. */

static bool fetch(cok_ecl *vm, uint32_t address, uint8_t *out)
{
    const uint8_t *b = code_byte(vm, address);
    if (b == NULL) {
        vm->status = COK_ECL_BAD_ADDRESS;
        return false;
    }
    *out = *b;
    return true;
}

/* Unpack a 6-bit string literal of count bytes after vm->ip into slot
 * (3775:1354): three bytes hold four codes; 0 is skipped, codes below 0x20
 * map to 0x40-0x5f. */
static bool unpack(cok_ecl *vm, char *slot, uint8_t count)
{
    size_t length = 0;
    uint8_t current = 0, previous = 0, code = 0;
    unsigned phase = 1;
    for (unsigned read = 0; read < count;) {
        if (phase < 4) {
            previous = current;
            if (!fetch(vm, ++vm->ip, &current)) return false;
            ++read;
        }
        switch (phase) {
        case 1: code = current >> 2; break;
        case 2: code = (uint8_t)((previous & 3) << 4 | current >> 4); break;
        case 3: code = (uint8_t)((previous & 15) << 2 | current >> 6); break;
        default: code = current & 0x3f; break;
        }
        phase = phase < 4 ? phase + 1 : 1;
        if (code != 0 && length < 255) slot[length++] = (char)(code < 0x20 ? code + 0x40 : code);
    }
    /* A final group of three bytes still holds its fourth code. */
    code = current & 0x3f;
    if (phase == 4 && code != 0 && length < 255)
        slot[length++] = (char)(code < 0x20 ? code + 0x40 : code);
    slot[length] = '\0';
    return true;
}

cok_ecl_status cok_ecl_operands(cok_ecl *vm, size_t count)
{
    if (count > COK_ECL_MAX_OPERANDS) return vm->status = COK_ECL_BAD_OPERAND;
    vm->operands = count;
    vm->strings = 0;
    for (size_t i = 0; i < count; ++i) {
        cok_ecl_operand *o = &vm->operand[i];
        if (!fetch(vm, vm->ip + 1u, &o->type) || !fetch(vm, vm->ip + 2u, &o->low))
            return vm->status;
        vm->ip = (uint16_t)(vm->ip + 2);
        if (o->type == COK_ECL_VAR || o->type == COK_ECL_WORD || o->type == COK_ECL_VAR_3 ||
            o->type == COK_ECL_STRVAR) {
            if (!fetch(vm, ++vm->ip, &o->high)) return vm->status;
        }
        if (o->type != COK_ECL_STRING && o->type != COK_ECL_STRVAR) continue;
        if (vm->strings == COK_ECL_MAX_STRINGS) return vm->status = COK_ECL_BAD_OPERAND;
        char *slot = vm->string[vm->strings++];
        if (o->type == COK_ECL_STRVAR)
            cok_ecl_load_string(vm, (uint16_t)(o->high << 8 | o->low), slot);
        else if (o->low == 0)
            slot[0] = '\0';
        else if (!unpack(vm, slot, o->low))
            return vm->status;
    }
    ++vm->ip;
    return COK_ECL_OK;
}

uint16_t cok_ecl_address(const cok_ecl *vm, size_t i)
{
    return (uint16_t)(vm->operand[i].high << 8 | vm->operand[i].low);
}

/* An operand's value (3775:0168). */
static uint16_t operand_value(const cok_ecl *vm, const cok_ecl_operand *o)
{
    uint16_t address = (uint16_t)(o->high << 8 | o->low);
    switch (o->type) {
    case COK_ECL_BYTE: return o->low;
    case COK_ECL_VAR: case COK_ECL_VAR_3: case COK_ECL_STRING:
        return load_sized(vm, address, wide_type(o->type));
    case COK_ECL_WORD: case COK_ECL_STRVAR: return address;
    default: return 0;
    }
}

uint16_t cok_ecl_value(const cok_ecl *vm, size_t i)
{
    return operand_value(vm, &vm->operand[i]);
}

uint16_t cok_ecl_header_value(const cok_ecl *vm, size_t i)
{
    assert(i < vm->header_operands);
    return operand_value(vm, &vm->header[i]);
}

/* Handlers. */

/* Comparison flags of a against b (3775:1afd, 3775:1a27). */
static void compare(cok_ecl *vm, int order)
{
    vm->flags[0] = order == 0;
    vm->flags[1] = order != 0;
    vm->flags[2] = order < 0;
    vm->flags[3] = order > 0;
    vm->flags[4] = order <= 0;
    vm->flags[5] = order >= 0;
}

static void compare_values(cok_ecl *vm, uint16_t a, uint16_t b)
{
    compare(vm, (a > b) - (a < b));
}

/* COMPARE AND's pair from operand i: their values equal, or where the
 * game compares strings and the first is one, the two strings next in
 * the slots (dkk 3632:0bed: the first pair's strings fill slots 1 and 2,
 * then the second's). */
static bool same_pair(const cok_ecl *vm, size_t i)
{
    if (!vm->rules->ecl.compare_and_strings || vm->operand[i].type < 0x80)
        return cok_ecl_value(vm, i) == cok_ecl_value(vm, i + 1);
    size_t slot = i == 2 && vm->operand[0].type >= 0x80 ? 2 : 0;
    return strcmp(vm->string[slot], vm->string[slot + 1]) == 0;
}

static void push_call(cok_ecl *vm, uint16_t target)
{
    if (vm->depth == COK_ECL_MAX_CALLS) {
        vm->status = COK_ECL_DEEP_CALLS;
        return;
    }
    vm->calls[vm->depth++] = vm->ip;
    vm->ip = target;
}

static void exit_script(cok_ecl *vm)
{
    if (vm->restore_character) {
        vm->character = vm->saved_character;
        vm->restore_character = false;
    }
    vm->stop = true;
    ++vm->ip;
    vm->depth = 0;
    vm->cursor = (cok_text_cursor){1, 0x11};
}

/* Skip the next instruction, as a failed IF does (3775:1e74). */
static void skip(cok_ecl *vm)
{
    uint8_t opcode;
    if (!fetch(vm, vm->ip, &opcode)) return;
    vm->opcode = opcode;
    int count = cok_ecl_skip_count(vm->rules, opcode);
    if (count == 0)
        ++vm->ip;
    else
        cok_ecl_operands(vm, (size_t)count);
}

/* After the opcode's header of operands, load as many more operands as
 * its last gives, over the header (2fd3:0f9d, 2fd3:116d, 2fd3:1c69).
 * Returns the header's first value. */
static uint16_t load_list(cok_ecl *vm)
{
    size_t count = header_count(vm);
    assert(count >= 1 && count <= sizeof vm->header / sizeof *vm->header);
    if (cok_ecl_operands(vm, count) != COK_ECL_OK) return 0;
    uint16_t first = cok_ecl_value(vm, 0);
    uint8_t items = (uint8_t)cok_ecl_value(vm, count - 1);
    memcpy(vm->header, vm->operand, count * sizeof *vm->operand);
    vm->header_operands = count;
    memcpy(vm->header_string, vm->string[0], sizeof vm->header_string);
    --vm->ip;
    cok_ecl_operands(vm, items);
    return first;
}

static void run_opcode(cok_ecl *vm)
{
    vm->header_operands = 0;
    vm->operands = 0;
    switch (cok_ecl_meaning(vm)) {
    case COK_ECL_EXIT: exit_script(vm); break;
    case COK_ECL_GOTO:
        if (cok_ecl_operands(vm, 1) == COK_ECL_OK) vm->ip = cok_ecl_address(vm, 0);
        return;
    case COK_ECL_GOSUB:
        if (cok_ecl_operands(vm, 1) == COK_ECL_OK) push_call(vm, cok_ecl_address(vm, 0));
        return;
    case COK_ECL_COMPARE:
        if (cok_ecl_operands(vm, 2) != COK_ECL_OK) return;
        if (vm->operand[0].type < 0x80 && vm->operand[1].type < 0x80) {
            compare_values(vm, cok_ecl_value(vm, 0), cok_ecl_value(vm, 1));
        } else {
            /* strcmp orders as Pascal does: bytes, then length. With one
             * string operand, slot 2 holds whatever it held before. */
            int order = strcmp(vm->string[0], vm->string[1]);
            compare(vm, (order > 0) - (order < 0));
        }
        return;
    case COK_ECL_ADD: case COK_ECL_SUBTRACT: case COK_ECL_DIVIDE: case COK_ECL_MULTIPLY: {
        if (cok_ecl_operands(vm, 3) != COK_ECL_OK) return;
        uint16_t a = cok_ecl_value(vm, 0), b = cok_ecl_value(vm, 1), result;
        unsigned meaning = cok_ecl_meaning(vm);
        if (meaning == COK_ECL_ADD) {
            result = (uint16_t)(a + b);
        } else if (meaning == COK_ECL_SUBTRACT) {
            result = (uint16_t)(b - a);
        } else if (meaning == COK_ECL_MULTIPLY) {
            result = (uint16_t)((unsigned)a * b);
        } else {
            if (b == 0) {
                vm->status = COK_ECL_DIVIDE_BY_ZERO;
                return;
            }
            result = a / b;
            cok_ecl_set_var(vm, COK_VAR_REMAINDER, a % b);
        }
        cok_ecl_store_to(vm, 2, result);
        return;
    }
    case COK_ECL_RANDOM: {
        if (cok_ecl_operands(vm, 2) != COK_ECL_OK) return;
        uint8_t range = (uint8_t)cok_ecl_value(vm, 0);
        if (range != 0xff) ++range;
        cok_ecl_store_to(vm, 1, (uint8_t)cok_tp_random(&vm->seed, range));
        return;
    }
    case COK_ECL_SAVE:
        if (cok_ecl_operands(vm, 2) != COK_ECL_OK) return;
        if (vm->operand[0].type < 0x80)
            cok_ecl_store_to(vm, 1, cok_ecl_value(vm, 0));
        else
            cok_ecl_store_string(vm, cok_ecl_address(vm, 1), vm->string[0]);
        return;
    case COK_ECL_RETURN:
        ++vm->ip;
        if (vm->depth == 0) {
            exit_script(vm);
            break;
        }
        vm->ip = vm->calls[--vm->depth];
        return;
    case COK_ECL_COMPARE_AND:
        memset(vm->flags, 0, sizeof vm->flags);
        if (cok_ecl_operands(vm, 4) != COK_ECL_OK) return;
        if (same_pair(vm, 0) && same_pair(vm, 2))
            vm->flags[0] = true;
        else
            vm->flags[1] = true;
        return;
    case COK_ECL_IF_EQUAL: case COK_ECL_IF_NOT_EQUAL: case COK_ECL_IF_LESS:
    case COK_ECL_IF_GREATER: case COK_ECL_IF_LESS_EQUAL: case COK_ECL_IF_GREATER_EQUAL:
        ++vm->ip;
        if (!vm->flags[cok_ecl_meaning(vm) - COK_ECL_IF_EQUAL]) skip(vm);
        return;
    case COK_ECL_NEWECL: {
        if (cok_ecl_operands(vm, 1) != COK_ECL_OK) return;
        cok_ecl_set_var(vm, COK_VAR_BLOCK, vm->block);
        vm->block = (uint8_t)cok_ecl_value(vm, 0);
        if (vm->hooks.load == NULL || !vm->hooks.load(vm, vm->block, vm->hooks.context)) {
            vm->status = COK_ECL_LOAD_FAILED;
            return;
        }
        if (cok_ecl_start(vm, !vm->keep_vars) != COK_ECL_OK) return;
        vm->stop = true;
        vm->reload = true;
        break;
    }
    case COK_ECL_ON_GOTO: case COK_ECL_ON_GOSUB: {
        bool jump = cok_ecl_meaning(vm) == COK_ECL_ON_GOTO;
        uint8_t choice = (uint8_t)load_list(vm);
        if (vm->status != COK_ECL_OK || choice >= vm->operands) return;
        if (jump)
            vm->ip = cok_ecl_address(vm, choice);
        else
            push_call(vm, cok_ecl_address(vm, choice));
        return;
    }
    case COK_ECL_GETTABLE: {
        if (cok_ecl_operands(vm, 3) != COK_ECL_OK) return;
        uint16_t from = (uint16_t)(cok_ecl_address(vm, 0) + (cok_ecl_value(vm, 1) & 0xff));
        /* Read as wide as the third operand's type makes it (dkk
         * 3632:0fb2), as the value stored. */
        cok_ecl_store_to(vm, 2, load_sized(vm, from, wide_type(vm->operand[2].type)));
        return;
    }
    case COK_ECL_SAVE_TABLE:
        if (cok_ecl_operands(vm, 3) != COK_ECL_OK) return;
        /* As wide as the value's operand's type makes it (dkk
         * 3632:1018). */
        store_sized(vm, (uint16_t)(cok_ecl_address(vm, 1) + cok_ecl_value(vm, 2)),
                    cok_ecl_value(vm, 0), wide_type(vm->operand[0].type));
        return;
    case COK_ECL_AND: case COK_ECL_OR: {
        if (cok_ecl_operands(vm, 3) != COK_ECL_OK) return;
        uint8_t a = (uint8_t)cok_ecl_value(vm, 0), b = (uint8_t)cok_ecl_value(vm, 1);
        uint8_t result = cok_ecl_meaning(vm) == COK_ECL_AND ? a & b : a | b;
        compare_values(vm, 0, result); /* 2fd3:0ed3 pushes 0 first */
        cok_ecl_store_to(vm, 2, result);
        return;
    }
    case COK_ECL_PRINT_RETURN:
        ++vm->ip;
        vm->cursor = (cok_text_cursor){1, vm->cursor.y + 1};
        break;
    default: {
        int count = cok_ecl_operand_count(vm->rules, vm->opcode);
        if (count == COK_ECL_HANG) {
            vm->status = COK_ECL_BAD_OPCODE;
            return;
        }
        if (count == COK_ECL_LIST)
            load_list(vm);
        else if (count == 0)
            ++vm->ip;
        else if (cok_ecl_operands(vm, (size_t)count) != COK_ECL_OK)
            return;
        break;
    }
    }
    if (vm->status == COK_ECL_OK && vm->hooks.opcode != NULL)
        vm->hooks.opcode(vm, vm->hooks.context);
}

cok_ecl_status cok_ecl_decode(cok_ecl *vm, uint16_t address)
{
    vm->status = COK_ECL_OK;
    vm->ip = address;
    vm->header_operands = 0;
    vm->operands = 0;
    uint8_t opcode;
    if (!fetch(vm, vm->ip, &opcode)) return vm->status;
    vm->opcode = opcode;
    int count = cok_ecl_operand_count(vm->rules, opcode);
    if (count == COK_ECL_HANG)
        vm->status = COK_ECL_BAD_OPCODE;
    else if (count == COK_ECL_LIST)
        load_list(vm);
    else if (count == 0)
        ++vm->ip;
    else
        cok_ecl_operands(vm, (size_t)count);
    return vm->status;
}

cok_ecl_status cok_ecl_start(cok_ecl *vm, bool reset_vars)
{
    vm->name_cleared = false;
    vm->c059 = 0x41;
    vm->c05f = 9;
    vm->c059_changed = true;
    vm->ip = COK_ECL_BASE;
    vm->depth = 0;
    memset(vm->flags, 0, sizeof vm->flags);
    for (unsigned i = 0; i < vm->rules->ecl.start_count; ++i)
        cok_ecl_set_var(vm, (cok_var)vm->rules->ecl.start[i].var, vm->rules->ecl.start[i].value);
    vm->status = COK_ECL_OK;
    for (size_t i = 0; i < 5; ++i) {
        if (cok_ecl_operands(vm, 1) != COK_ECL_OK) return vm->status;
        vm->vectors[i] = cok_ecl_address(vm, 0);
    }
    if (reset_vars) {
        for (size_t i = 0; i < vm->rules->ecl.vars[COK_VAR_LOCALS].count; ++i)
            cok_ecl_set_var_at(vm, COK_VAR_LOCALS, i, 0);
        for (size_t i = 0; i < vm->rules->ecl.vars[COK_VAR_LOCALS_GAME].count; ++i)
            cok_ecl_set_var_at(vm, COK_VAR_LOCALS_GAME, i, 0);
    }
    return COK_ECL_OK;
}

void cok_ecl_exit(cok_ecl *vm)
{
    exit_script(vm);
}

cok_ecl_status cok_ecl_run(cok_ecl *vm, uint16_t address)
{
    vm->ip = address;
    vm->stop = false;
    vm->status = COK_ECL_OK;
    while (!vm->stop && !vm->abort && !vm->halt) {
        if (vm->hooks.trace != NULL) vm->hooks.trace(vm, vm->hooks.context);
        uint8_t opcode;
        if (!fetch(vm, vm->ip, &opcode)) break;
        vm->previous = vm->opcode;
        vm->opcode = opcode;
        run_opcode(vm);
        if (vm->status != COK_ECL_OK) break;
        if (vm->hooks.executed != NULL) vm->hooks.executed(vm, vm->hooks.context);
    }
    cok_ecl_status status = vm->status;
    vm->stop = false;
    return status;
}
