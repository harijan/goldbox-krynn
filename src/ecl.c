#include "ecl.h"

#include <string.h>

static const char *const names[COK_ECL_OPCODES] = {
    "EXIT", "GOTO", "GOSUB", "COMPARE", "ADD", "SUBTRAT", "DIVIDE", "MULTIPLY", "RANDOM", "SAVE",
    "LOAD CHARACTER", "LOAD MONSTER", "SETUP MONSTER", "APPROACH", "PICTURE", "INPUT NUMBER",
    "INPUT STRING", "PRINT", "PRINTCLEAR", "RETURN", "COMPARE AND", "VERTICAL MENU", "IF =",
    "IF <>", "IF <", "IF >", "IF <=", "IF >=", "CLEARMONSTERS", "PARTYSTRENGTH", "CHECKPARTY",
    NULL, "NEWECL", "LOAD FILES", "PARTY SURPRISE", "SURPRISE", "COMBAT", "ON GOTO", "ON GOSUB",
    "TREASURE", "ROB", "ENCOUNTER MENU", "GETTABLE", "HORIZONTAL MENU", "PARLAY", "CALL",
    "DAMAGE", "AND", "OR", "SPRITE OFF", "FIND ITEM", "PRINT RETURN", "ECL CLOCK", "SAVE TABLE",
    "ADD NPC", "LOAD PIECES", "PROGRAM", "WHO", "DELAY", "SPELL", "PROTECTION", "CLEAR BOX",
    "DUMP", "FIND SPECIAL", "DESTROY ITEMS", "ADD EP",
};

/* Operands each handler loads (2fd3:0050-36dc); unlisted opcodes load none. */
static const signed char counts[COK_ECL_OPCODES] = {
    [COK_ECL_GOTO] = 1, [COK_ECL_GOSUB] = 1, [COK_ECL_COMPARE] = 2, [COK_ECL_ADD] = 3,
    [COK_ECL_SUBTRACT] = 3, [COK_ECL_DIVIDE] = 3, [COK_ECL_MULTIPLY] = 3, [COK_ECL_RANDOM] = 2,
    [COK_ECL_SAVE] = 2, [COK_ECL_LOAD_CHARACTER] = 1, [COK_ECL_LOAD_MONSTER] = 3,
    [COK_ECL_SETUP_MONSTER] = 3, [COK_ECL_PICTURE] = 1, [COK_ECL_INPUT_NUMBER] = 2,
    [COK_ECL_INPUT_STRING] = 2, [COK_ECL_PRINT] = 1, [COK_ECL_PRINTCLEAR] = 1,
    [COK_ECL_COMPARE_AND] = 4, [COK_ECL_VERTICAL_MENU] = COK_ECL_LIST,
    [COK_ECL_PARTYSTRENGTH] = 1, [COK_ECL_CHECKPARTY] = 6, [COK_ECL_UNUSED_1F] = COK_ECL_HANG,
    [COK_ECL_NEWECL] = 1, [COK_ECL_LOAD_FILES] = 3, [COK_ECL_PARTY_SURPRISE] = 2,
    [COK_ECL_SURPRISE] = 4, [COK_ECL_ON_GOTO] = COK_ECL_LIST, [COK_ECL_ON_GOSUB] = COK_ECL_LIST,
    [COK_ECL_TREASURE] = 8, [COK_ECL_ROB] = 3, [COK_ECL_ENCOUNTER_MENU] = 14,
    [COK_ECL_GETTABLE] = 3, [COK_ECL_HORIZONTAL_MENU] = COK_ECL_LIST, [COK_ECL_PARLAY] = 6,
    [COK_ECL_CALL] = 1, [COK_ECL_DAMAGE] = 5, [COK_ECL_AND] = 3, [COK_ECL_OR] = 3,
    [COK_ECL_FIND_ITEM] = 1, [COK_ECL_ECL_CLOCK] = 2, [COK_ECL_SAVE_TABLE] = 3,
    [COK_ECL_ADD_NPC] = 2, [COK_ECL_LOAD_PIECES] = 3, [COK_ECL_PROGRAM] = 1, [COK_ECL_WHO] = 1,
    [COK_ECL_SPELL] = 3, [COK_ECL_PROTECTION] = COK_ECL_HANG, [COK_ECL_FIND_SPECIAL] = 1,
    [COK_ECL_DESTROY_ITEMS] = 1, [COK_ECL_ADD_EP] = 2,
};

const char *cok_ecl_opcode_name(unsigned opcode)
{
    return opcode < COK_ECL_OPCODES ? names[opcode] : NULL;
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
    }
    return "unknown status";
}

/* Operands a failed IF skips with the next instruction (3775:1e74).
 * VERTICAL MENU, ON GOTO, ON GOSUB, HORIZONTAL MENU and opcodes without
 * handlers skip none, and ECL CLOCK, ADD NPC and PROTECTION one, whatever
 * their handlers load. */
static const uint8_t skips[COK_ECL_OPCODES] = {
    [COK_ECL_GOTO] = 1, [COK_ECL_GOSUB] = 1, [COK_ECL_LOAD_CHARACTER] = 1,
    [COK_ECL_PICTURE] = 1, [COK_ECL_PRINT] = 1, [COK_ECL_PRINTCLEAR] = 1,
    [COK_ECL_PARTYSTRENGTH] = 1, [COK_ECL_NEWECL] = 1, [COK_ECL_CALL] = 1,
    [COK_ECL_FIND_ITEM] = 1, [COK_ECL_ECL_CLOCK] = 1, [COK_ECL_ADD_NPC] = 1,
    [COK_ECL_PROGRAM] = 1, [COK_ECL_WHO] = 1, [COK_ECL_PROTECTION] = 1,
    [COK_ECL_FIND_SPECIAL] = 1, [COK_ECL_DESTROY_ITEMS] = 1,
    [COK_ECL_COMPARE] = 2, [COK_ECL_RANDOM] = 2, [COK_ECL_SAVE] = 2,
    [COK_ECL_INPUT_NUMBER] = 2, [COK_ECL_INPUT_STRING] = 2, [COK_ECL_UNUSED_1F] = 2,
    [COK_ECL_PARTY_SURPRISE] = 2, [COK_ECL_ADD_EP] = 2,
    [COK_ECL_ADD] = 3, [COK_ECL_SUBTRACT] = 3, [COK_ECL_DIVIDE] = 3, [COK_ECL_MULTIPLY] = 3,
    [COK_ECL_LOAD_MONSTER] = 3, [COK_ECL_SETUP_MONSTER] = 3, [COK_ECL_LOAD_FILES] = 3,
    [COK_ECL_ROB] = 3, [COK_ECL_GETTABLE] = 3, [COK_ECL_AND] = 3, [COK_ECL_OR] = 3,
    [COK_ECL_SAVE_TABLE] = 3, [COK_ECL_LOAD_PIECES] = 3, [COK_ECL_SPELL] = 3,
    [COK_ECL_COMPARE_AND] = 4, [COK_ECL_SURPRISE] = 4, [COK_ECL_DAMAGE] = 5,
    [COK_ECL_CHECKPARTY] = 6, [COK_ECL_PARLAY] = 6, [COK_ECL_TREASURE] = 8,
    [COK_ECL_ENCOUNTER_MENU] = 14,
};

int cok_ecl_skip_count(unsigned opcode)
{
    return opcode < COK_ECL_OPCODES ? skips[opcode] : 0;
}

int cok_ecl_operand_count(unsigned opcode)
{
    return opcode < COK_ECL_OPCODES ? counts[opcode] : COK_ECL_HANG;
}

uint16_t cok_tp_random(uint32_t *seed, uint16_t range)
{
    *seed = *seed * 0x08088405u + 1;
    if (range == 0)
        return 0;
    return (uint16_t)((*seed >> 16) % range);
}

void cok_ecl_init(cok_ecl *vm, const cok_ecl_hooks *hooks)
{
    memset(vm, 0, sizeof *vm);
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

static uint16_t byte_at(const uint8_t *record, unsigned offset)
{
    return record[offset];
}

static uint16_t signed_at(const uint8_t *record, unsigned offset)
{
    return (uint16_t)(int16_t)(int8_t)record[offset];
}

static uint16_t word_at(const uint8_t *record, unsigned offset)
{
    return (uint16_t)(record[offset] | record[offset + 1] << 8);
}

static void set_word(uint8_t *record, unsigned offset, uint16_t value)
{
    record[offset] = (uint8_t)value;
    record[offset + 1] = (uint8_t)(value >> 8);
}

/* Character fields read through 0x7c00-0x7fff (3775:07d9); *found is false
 * for addresses that read the variable itself. */
static uint16_t character_field(const cok_ecl *vm, uint16_t address, bool *found)
{
    const uint8_t *c = vm->character;
    unsigned field = address - 0x7c00u;
    *found = true;
    switch (field) {
    case 0x312: return vm->file;
    case 0x33e: return vm->mem7c00[0x33e];
    case 0x2b1: case 0x2b4: case 0xc9:
        if (vm->hooks.character_value == NULL) return 0;
        return vm->hooks.character_value((cok_ecl *)vm, address, vm->hooks.context);
    case 0x100:
        if (c == NULL || vm->missing_character) return 0;
        switch (c[0x188]) {
        case 0: return 1;
        case 3: return 6;
        case 4: return 5;
        case 5: return 4;
        case 6: return 3;
        case 7: return 7;
        case 8: return 2;
        default: return 0;
        }
    default: break;
    }
    if (field >= 0xa5 && field <= 0xac) return c == NULL ? 0 : byte_at(c, field - 0xa4 + 0xda);
    static const struct {
        uint16_t field, offset;
        uint8_t kind; /* 0 byte, 1 signed byte, 2 word */
    } fields[] = {
        {0x15, 0x13, 0}, {0x18, 0x19, 0}, {0x19, 0x1b, 0}, {0x61, 0x5c, 0}, {0x72, 0x5a, 1},
        {0x73, 0x5b, 1}, {0x76, 0x62, 0}, {0x9b, 0xd1, 0}, {0xa0, 0xd6, 1}, {0xb8, 0xe7, 0},
        {0xbb, 0xeb, 2}, {0xbd, 0xed, 2}, {0xbf, 0xef, 2}, {0xc1, 0xf1, 2}, {0xc3, 0xf3, 2},
        {0xcc, 0xfb, 1}, {0xcd, 0x100, 1}, {0xd0, 0xfd, 1}, {0xd6, 0x109, 1}, {0xd8, 0x10a, 1},
        {0xf7, 0x130, 2}, {0xf9, 0x132, 0}, {0x119, 0x197, 0}, {0x11b, 0x198, 0},
    };
    for (size_t i = 0; i < sizeof fields / sizeof *fields; ++i) {
        if (fields[i].field != field) continue;
        if (c == NULL) return 0;
        if (fields[i].kind == 0) return byte_at(c, fields[i].offset);
        if (fields[i].kind == 1) return signed_at(c, fields[i].offset);
        return word_at(c, fields[i].offset);
    }
    if (field == 0x10c) {
        if (c == NULL) return 0;
        if (c[0x18a] == 0 && c[0x18b] != 0) return 0x80;
        return c[0x18a] == 1 ? 0x81 : 0;
    }
    /* The original leaves the result unset for 0x10d and for unlisted
     * values of 0x2cf; both read as 0 here. */
    if (field == 0x10d) return 0;
    if (field == 0x2cf) {
        static const uint8_t table[26] = {0,  0,  0,  0,  5,  10, 15, 20, 25, 25, 25, 25, 25,
                                          30, 35, 40, 50, 55, 60, 60, 60, 60, 60, 60, 60, 60};
        return c == NULL || c[0x1b] >= sizeof table ? 0 : table[c[0x1b]];
    }
    *found = false;
    return 0;
}

uint16_t cok_ecl_load_var(const cok_ecl *vm, uint16_t address)
{
    switch (area_of(address)) {
    case AREA:
        return address == 0x4bea ? vm->direction : vm->mem4b00[address - 0x4b00];
    case GAME: {
        bool found;
        uint16_t value = character_field(vm, address, &found);
        return found ? value : vm->mem7c00[address - 0x7c00];
    }
    case SCRATCH: return vm->mem7a00[address - 0x7a00];
    case CODE: return vm->code[address - COK_ECL_BASE];
    default: break;
    }
    /* Unlisted special addresses leave the original's result unset; 0 here. */
    switch (address) {
    case 0xb1: return vm->value_b1;
    case 0xfb: return vm->value_fb;
    case 0xfc: return vm->value_fc;
    case 0x33d: return vm->direction;
    case 0xc04b: return (uint16_t)(int16_t)vm->map_x;
    case 0xc04c: return (uint16_t)(int16_t)vm->map_y;
    case 0xc04d: return vm->mem4b00[0xe6] == 0 ? vm->direction : vm->direction >> 1;
    case 0xc04e: return vm->ahead;
    case 0xc04f: return vm->square;
    default: return 0;
    }
}

/* Character fields written through 0x7c00-0x7fff (3775:0c00). */
static void store_character(cok_ecl *vm, uint16_t address, uint16_t value)
{
    unsigned field = address - 0x7c00u;
    if (field == 0) {
        if (value == 0) vm->name_cleared = true;
        return;
    }
    if (field == 0x312) {
        /* 169c:0a3e: pick the ECL file holding the next block. */
        vm->previous_file = vm->file;
        vm->file = (uint8_t)(((value & 0xff) + 1) >> 1);
        return;
    }
    uint8_t *c = vm->character;
    if (c == NULL) return;
    if (field >= 0x20 && field <= 0x59) {
        c[field - 0x20 + 0x1e] = (uint8_t)value;
        return;
    }
    switch (field) {
    case 0xb8: c[0xe7] = (uint8_t)(value > 0xb2 ? value - 0x32 : value); break;
    case 0xbb: set_word(c, 0xeb, value); break;
    case 0xbd: set_word(c, 0xed, value); break;
    case 0xbf: set_word(c, 0xef, value); break;
    case 0xc1: set_word(c, 0xf1, value); break;
    case 0xc3: set_word(c, 0xf3, value); break;
    case 0xf7: set_word(c, 0x130, value); break;
    case 0xf9: c[0x132] = (uint8_t)value; break;
    case 0x119: c[0x197] = (uint8_t)value; break;
    case 0x100:
        if (value == 1) {
            c[0x188] = 0;
            c[0x189] = 1;
        }
        break;
    case 0x10c:
        if (value == 0 || value == 0x80 || value == 0x81) {
            c[0x18a] = value == 0x81;
            c[0x18b] = value != 0;
        }
        break;
    default: break;
    }
}

void cok_ecl_store(cok_ecl *vm, uint16_t address, uint16_t value)
{
    switch (area_of(address)) {
    case AREA:
        if (address == 0x4bfd || address == 0x4bfe) {
            vm->area_changed = true;
        } else if (address == 0x4be6 && vm->mem4b00[0xe6] != value) {
            vm->last_mode = vm->mode;
            vm->mode = value == 0 ? 3 : 4;
        }
        vm->mem4b00[address - 0x4b00] = value;
        break;
    case GAME:
        vm->mem7c00[address - 0x7c00] = value;
        store_character(vm, address, value);
        break;
    case SCRATCH: vm->mem7a00[address - 0x7a00] = value; break;
    case CODE: vm->code[address - COK_ECL_BASE] = (uint8_t)value; break;
    default:
        switch (address) {
        case 0x3de: vm->value_3de = value; break;
        case 0xb8: vm->value_b8 = value; break;
        case 0xb9: vm->value_b9 = value; break;
        case 0xc04b: vm->map_x = (int8_t)value; vm->view_changed = true; break;
        case 0xc04c: vm->map_y = (int8_t)value; vm->view_changed = true; break;
        case 0xc04d: vm->direction = (uint8_t)(value % 4 * 2); vm->view_changed = true; break;
        case 0xc059: vm->c059 = (uint8_t)value; vm->c059_changed = true; break;
        case 0xc05f: vm->c05f = (uint8_t)value; vm->c059_changed = true; break;
        default: break;
        }
        break;
    }
    if (vm->hooks.stored != NULL) vm->hooks.stored(vm, address, value, vm->hooks.context);
}

void cok_ecl_load_string(const cok_ecl *vm, uint16_t address, char out[256])
{
    int area = area_of(address);
    unsigned length = 0;
    if (address == 0x7c00) {
        if (vm->character != NULL) {
            length = vm->character[0] > 15 ? 15 : vm->character[0];
            memcpy(out, vm->character + 1, length);
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
            unsigned n = length > 15 ? 15 : length;
            vm->character[0] = (uint8_t)n;
            memcpy(vm->character + 1, text, n);
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

uint16_t cok_ecl_value(const cok_ecl *vm, size_t i)
{
    switch (vm->operand[i].type) {
    case COK_ECL_BYTE: return vm->operand[i].low;
    case COK_ECL_VAR: case COK_ECL_VAR_3: case COK_ECL_STRING:
        return cok_ecl_load_var(vm, cok_ecl_address(vm, i));
    case COK_ECL_WORD: case COK_ECL_STRVAR: return cok_ecl_address(vm, i);
    default: return 0;
    }
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
    int count = cok_ecl_skip_count(opcode);
    if (count == 0)
        ++vm->ip;
    else
        cok_ecl_operands(vm, (size_t)count);
}

/* After a header of count operands, load as many more operands as header
 * operand which gives, over the header (2fd3:0f9d, 2fd3:116d, 2fd3:1c69).
 * Returns the header's first value. */
static uint16_t load_list(cok_ecl *vm, size_t count, size_t which)
{
    if (cok_ecl_operands(vm, count) != COK_ECL_OK) return 0;
    uint16_t first = cok_ecl_value(vm, 0);
    uint8_t items = (uint8_t)cok_ecl_value(vm, which);
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
    switch (vm->opcode) {
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
        if (vm->opcode == COK_ECL_ADD) {
            result = (uint16_t)(a + b);
        } else if (vm->opcode == COK_ECL_SUBTRACT) {
            result = (uint16_t)(b - a);
        } else if (vm->opcode == COK_ECL_MULTIPLY) {
            result = (uint16_t)((unsigned)a * b);
        } else {
            if (b == 0) {
                vm->status = COK_ECL_DIVIDE_BY_ZERO;
                return;
            }
            result = a / b;
            vm->mem7c00[0x33f] = a % b;
        }
        cok_ecl_store(vm, cok_ecl_address(vm, 2), result);
        return;
    }
    case COK_ECL_RANDOM: {
        if (cok_ecl_operands(vm, 2) != COK_ECL_OK) return;
        uint8_t range = (uint8_t)cok_ecl_value(vm, 0);
        if (range != 0xff) ++range;
        cok_ecl_store(vm, cok_ecl_address(vm, 1), (uint8_t)cok_tp_random(&vm->seed, range));
        return;
    }
    case COK_ECL_SAVE:
        if (cok_ecl_operands(vm, 2) != COK_ECL_OK) return;
        if (vm->operand[0].type < 0x80)
            cok_ecl_store(vm, cok_ecl_address(vm, 1), cok_ecl_value(vm, 0));
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
        if (cok_ecl_value(vm, 0) == cok_ecl_value(vm, 1) &&
            cok_ecl_value(vm, 2) == cok_ecl_value(vm, 3))
            vm->flags[0] = true;
        else
            vm->flags[1] = true;
        return;
    case COK_ECL_IF_EQUAL: case COK_ECL_IF_NOT_EQUAL: case COK_ECL_IF_LESS:
    case COK_ECL_IF_GREATER: case COK_ECL_IF_LESS_EQUAL: case COK_ECL_IF_GREATER_EQUAL:
        ++vm->ip;
        if (!vm->flags[vm->opcode - COK_ECL_IF_EQUAL]) skip(vm);
        return;
    case COK_ECL_NEWECL: {
        if (cok_ecl_operands(vm, 1) != COK_ECL_OK) return;
        vm->mem4b00[0xf2] = vm->block;
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
        uint8_t choice = (uint8_t)load_list(vm, 2, 1);
        if (vm->status != COK_ECL_OK || choice >= vm->operands) return;
        if (vm->opcode == COK_ECL_ON_GOTO)
            vm->ip = cok_ecl_address(vm, choice);
        else
            push_call(vm, cok_ecl_address(vm, choice));
        return;
    }
    case COK_ECL_GETTABLE: {
        if (cok_ecl_operands(vm, 3) != COK_ECL_OK) return;
        uint16_t from = (uint16_t)(cok_ecl_address(vm, 0) + (cok_ecl_value(vm, 1) & 0xff));
        cok_ecl_store(vm, cok_ecl_address(vm, 2), cok_ecl_load_var(vm, from));
        return;
    }
    case COK_ECL_SAVE_TABLE:
        if (cok_ecl_operands(vm, 3) != COK_ECL_OK) return;
        cok_ecl_store(vm, (uint16_t)(cok_ecl_address(vm, 1) + cok_ecl_value(vm, 2)),
                      cok_ecl_value(vm, 0));
        return;
    case COK_ECL_AND: case COK_ECL_OR: {
        if (cok_ecl_operands(vm, 3) != COK_ECL_OK) return;
        uint8_t a = (uint8_t)cok_ecl_value(vm, 0), b = (uint8_t)cok_ecl_value(vm, 1);
        uint8_t result = vm->opcode == COK_ECL_AND ? a & b : a | b;
        compare_values(vm, 0, result); /* 2fd3:0ed3 pushes 0 first */
        cok_ecl_store(vm, cok_ecl_address(vm, 2), result);
        return;
    }
    case COK_ECL_PRINT_RETURN:
        ++vm->ip;
        vm->cursor = (cok_text_cursor){1, vm->cursor.y + 1};
        break;
    case COK_ECL_VERTICAL_MENU: load_list(vm, 3, 2); break;
    case COK_ECL_HORIZONTAL_MENU: load_list(vm, 2, 1); break;
    default: {
        int count = cok_ecl_operand_count(vm->opcode);
        if (count == COK_ECL_HANG) {
            vm->status = COK_ECL_BAD_OPCODE;
            return;
        }
        if (count == 0)
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
    int count = cok_ecl_operand_count(opcode);
    if (count == COK_ECL_HANG)
        vm->status = COK_ECL_BAD_OPCODE;
    else if (count == COK_ECL_LIST && opcode == COK_ECL_VERTICAL_MENU)
        load_list(vm, 3, 2);
    else if (count == COK_ECL_LIST)
        load_list(vm, 2, 1);
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
    vm->mem7c00[0x2e1] = 0xff;
    vm->mem7c00[0x2d2] = 0;
    vm->mem7c00[0x2d3] = 0;
    vm->mem4b00[0xe5] = 0;
    vm->status = COK_ECL_OK;
    for (size_t i = 0; i < 5; ++i) {
        if (cok_ecl_operands(vm, 1) != COK_ECL_OK) return vm->status;
        vm->vectors[i] = cok_ecl_address(vm, 0);
    }
    if (reset_vars) {
        memset(vm->mem4b00 + 0x100, 0, 0x20 * sizeof *vm->mem4b00);
        memset(vm->mem7c00 + 0x379, 0, 10 * sizeof *vm->mem7c00);
    }
    return COK_ECL_OK;
}

cok_ecl_status cok_ecl_run(cok_ecl *vm, uint16_t address)
{
    vm->ip = address;
    vm->stop = false;
    vm->status = COK_ECL_OK;
    while (!vm->stop && !vm->abort) {
        if (vm->hooks.trace != NULL) vm->hooks.trace(vm, vm->hooks.context);
        uint8_t opcode;
        if (!fetch(vm, vm->ip, &opcode)) break;
        vm->previous = vm->opcode;
        vm->opcode = opcode;
        run_opcode(vm);
        if (vm->status != COK_ECL_OK) break;
    }
    cok_ecl_status status = vm->status;
    vm->stop = false;
    return status;
}
