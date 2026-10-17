#include "debug.h"

#include "adventure.h"
#include "screen.h"

#include <stdio.h>
#include <string.h>

/* DEBUG.TXT's buffer written out (the Text file's InOutFunc). */
static void write_out(cok_debug_file *f)
{
    if (f->file != NULL && f->used > 0) {
        fwrite(f->buffer, 1, f->used, f->file);
        fflush(f->file);
    }
    f->used = 0;
}

static void put(cok_debug_file *f, const char *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        f->buffer[f->used++] = (uint8_t)bytes[i];
        if (f->used == COK_DEBUG_BUFFER) write_out(f);
    }
}

void cok_debug_write(cok_adventure *game, const char *line)
{
    cok_debug_file *f = &game->debug_file;
    if (!f->open) return;
    cok_adventure_log(game, "debug.txt", line);
    put(f, line, strlen(line));
    put(f, "\r\n", 2);
}

void cok_debug_close(cok_adventure *game)
{
    cok_debug_file *f = &game->debug_file;
    if (!f->open) return;
    write_out(f);
    if (f->file != NULL) fclose(f->file);
    f->file = NULL;
    f->open = false;
}

/* Assign(DS:8864, 'debug.txt') (1a46:04f7), then Rewrite (1a46:056c).
 * Assign starts the buffer afresh, so that a file the reset left open
 * loses what it held; DOS's create truncates the file. The game reads no
 * I/O result ({$I-}): a file that cannot be made is not written. DOS
 * keeps the name upper-case, DEBUG.TXT, as the port names the saved games
 * it writes; nothing reads it back. */
static void open_file(cok_adventure *game)
{
    cok_debug_file *f = &game->debug_file;
    if (f->file != NULL) fclose(f->file); /* The abandoned handle: nothing more is written. */
    f->file = NULL;
    f->used = 0;
    f->open = true;
    if (game->save_dir[0] == '\0') {
        char text[80];
        snprintf(text, sizeof text, "%s opened (no saves directory: not written)",
                 game->rules->saves.debug);
        cok_adventure_log(game, "debug", text);
        return;
    }
    char path[sizeof game->save_dir + 16];
    snprintf(path, sizeof path, "%s/%s", game->save_dir, game->rules->saves.debug);
    f->file = fopen(path, "wb");
    char text[sizeof path + 40];
    snprintf(text, sizeof text, f->file != NULL ? "%s opened" : "%s cannot be made", path);
    cok_adventure_log(game, "debug", text);
}

void cok_debug_name(const uint8_t *record, char out[256])
{
    memcpy(out, record + 1, record[0]);
    out[record[0]] = '\0';
}

/* 1614:007b for each record of the list (Ctrl-Z): a line for each of its
 * effects, the name its record's string (+0). */
static void effects(cok_adventure *game)
{
    for (size_t i = 0; i < game->party.count; ++i) {
        const cok_character *c = game->party.members[i];
        char name[256];
        cok_debug_name(c->record, name);
        for (const cok_effect *e = c->effects; e != NULL; e = e->next) {
            char line[300];
            snprintf(line, sizeof line, "who: %s  sp#: %u", name, e->id);
            cok_debug_write(game, line);
        }
    }
}

int cok_debug_key(cok_adventure *game, int key)
{
    if (!game->woof) return key; /* ParamStr(1) is not 'Woof' (1614:0320). */
    if (key == 4) {
        game->debug = !game->debug;
        if (game->debug) {
            open_file(game);
            cok_adventure_log(game, "debug", "on");
        } else {
            cok_debug_close(game);
            cok_adventure_log(game, "debug", "off");
        }
    } else if (key == 0x1a) {
        if (game->debug) effects(game);
    } else if (key == 3) {
        cok_adventure_log(game, "quit", "to DOS");
        cok_adventure_quit(game);
        return -1;
    }
    return key;
}

void cok_debug_opcode_name(unsigned opcode, char out[16])
{
    const char *name = cok_ecl_opcode_name(opcode);
    if (opcode == COK_ECL_IF_EQUAL)
        snprintf(out, 16, "IF = ");
    else if (name != NULL)
        snprintf(out, 16, "%s", name);
    else
        snprintf(out, 16, "BULLSHIT %u", opcode & 0xffu);
}

/* Str of a byte, as Write and Str give it with no width. */
static void number(char out[8], unsigned value)
{
    snprintf(out, 8, "%u", value);
}

void cok_debug_trace(cok_adventure *game, const char *text, uint8_t a, uint8_t b, uint8_t c)
{
    if (!game->debug || game->input_ended || game->quit || game->vm.status != COK_ECL_OK) return;
    char shown[41], numbers[3][8], line[48];
    snprintf(shown, sizeof shown, "%s", text);
    number(numbers[0], a);
    number(numbers[1], b);
    number(numbers[2], c);
    snprintf(line, sizeof line, "%s , %s , %s", numbers[0], numbers[1], numbers[2]);
    char logged[100];
    snprintf(logged, sizeof logged, "%s | %s", line, shown);
    cok_adventure_log(game, "debug", logged);
    cok_text_clear(&game->screen, &game->font, 40, 0, 0, 0);     /* 1521:0b60 */
    cok_text_string(&game->screen, &game->font, line, 0, 0, 14, 0); /* 1521:0353 */
    cok_adventure_prompt_colour(game, shown, 14); /* 1521:096c, a key through 1614:025b */
    cok_text_clear(&game->screen, &game->font, 40, 0, 0, 0); /* 1521:0b60 */
}

bool cok_debug_item(cok_adventure *game, const uint8_t *item)
{
    static const char *const labels[16] = {
        "itemptr:      ", "namenum(1):   ", "namenum(2):   ", "namenum(3):   ",
        "plus:         ", "plussave:     ", "ready:        ", "identified:   ",
        "cursed:       ", "value:        ", "special(1):   ", "special(2):   ",
        "special(3):   ", "missile type: ", "max range :   ", "item type :    ",
    };
    uint8_t type = item[0x2e];
    if (type >= COK_ITEM_TYPES) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "View's item (546c:12be) reads type %u's row past the table of item "
                           "types (DS:5886)", type);
        return false;
    }
    const uint8_t *row = game->item_types.type[type];
    unsigned values[16];
    for (size_t i = 0; i < 9; ++i) values[i] = item[0x2e + i];      /* +0x2e-+0x36 */
    values[9] = (unsigned)(item[0x3a] | item[0x3b] << 8);          /* 6346:1203 */
    for (size_t i = 0; i < 3; ++i) values[10 + i] = item[0x3c + i]; /* +0x3c-+0x3e */
    values[13] = row[14];                                          /* DS:5894 */
    values[14] = row[12];                                          /* DS:5892 */
    values[15] = row[0];                                           /* DS:5886 */
    uint16_t moons[3];
    for (size_t i = 0; i < 3; ++i) moons[i] = game->vm.mem4b00[0x1f9 + i];
    cok_screen_frame(&game->screen, &game->view.tiles[4], moons, true); /* 1128:0000 */
    for (int i = 0; i < 16; ++i) {
        char value[8], line[40];
        snprintf(value, sizeof value, "%u", values[i]);
        snprintf(line, sizeof line, "%s%s", labels[i], value);
        cok_adventure_log(game, "debug", line);
        cok_text_string(&game->screen, &game->font, labels[i], 1, 1 + i, 10, 0);
        cok_text_string(&game->screen, &game->font, value, 0x14, 1 + i, 10, 0);
    }
    cok_adventure_log(game, "debug", "press a key");
    cok_adventure_prompt_colour(game, "press a key", 10); /* 1521:096c */
    return true;
}

void cok_debug_reset(cok_adventure *game)
{
    game->debug = false; /* 3e99:0977; the file stays open, its buffer unwritten */
}

void cok_debug_end(cok_adventure *game)
{
    if (game->debug) cok_debug_close(game);
    if (game->debug_file.file != NULL) fclose(game->debug_file.file);
    game->debug_file = (cok_debug_file){0};
}
