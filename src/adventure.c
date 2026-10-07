#include "adventure.h"

#include "dax.h"
#include "screen.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const cok_text_window text_window = {1, 0x11, 0x26, 0x16};

static void fail(cok_adventure *game, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(game->error, sizeof game->error, format, args);
    va_end(args);
}

static void log_text(cok_adventure *game, const char *kind, const char *text)
{
    if (game->hooks.log != NULL) game->hooks.log(game, kind, text, game->hooks.context);
}

static void wait_ms(cok_adventure *game, unsigned ms)
{
    if (game->hooks.delay != NULL) game->hooks.delay(game, ms, game->hooks.context);
}

/* Keys go through here so that the run stops when they run out. */
static int read_key(void *context)
{
    cok_adventure *game = context;
    int key = game->keys.read == NULL ? -1 : game->keys.read(game->keys.context);
    if (key < 0) {
        game->input_ended = true;
        game->vm.abort = true;
    }
    return key;
}

static cok_keyboard keyboard(cok_adventure *game)
{
    return (cok_keyboard){read_key, game};
}

static void menu_special(uint8_t scan, void *context);
static int menu_read(cok_adventure *game, const char *prompt, const char *items, bool *special);

/* Read a record by id from <name>.DAX (169c:088e). */
static uint8_t *read_record(cok_adventure *game, const char *name, uint8_t id, size_t *size)
{
    char path[sizeof game->assets + 32];
    snprintf(path, sizeof path, "%s/%s.DAX", game->assets, name);
    dax_archive archive = {0};
    dax_status status = dax_open(path, &archive);
    if (status != DAX_OK) {
        fail(game, "%s: %s", path, dax_status_string(status));
        return NULL;
    }
    uint8_t *data = NULL;
    for (size_t i = 0; i < archive.count; ++i) {
        dax_record record;
        status = dax_record_at(&archive, i, &record);
        if (status != DAX_OK) break;
        if (record.id != id) continue;
        data = malloc(record.decoded_size == 0 ? 1 : record.decoded_size);
        if (data == NULL) {
            fail(game, "%s: out of memory", path);
            break;
        }
        status = dax_decode(record.packed, record.packed_size, data, record.decoded_size);
        if (status != DAX_OK) {
            free(data);
            data = NULL;
            break;
        }
        *size = record.decoded_size;
        break;
    }
    if (data == NULL && status != DAX_OK)
        fail(game, "%s record %u: %s", path, id, dax_status_string(status));
    else if (data == NULL && game->error[0] == '\0')
        fail(game, "%s has no record %u", path, id);
    dax_close(&archive);
    return data;
}

uint8_t *cok_adventure_record(cok_adventure *game, const char *name, unsigned file,
                              uint8_t id, size_t *size)
{
    char label[32];
    snprintf(label, sizeof label, "%.20s%u", name, file);
    return read_record(game, label, id, size);
}

/* Load a record of one image or one group of frames from <name>.DAX into
 * picture (127f:0111); transparent is as for cok_picture_load. The old
 * picture is freed first. */
static bool load_single(cok_adventure *game, const char *name, uint8_t id, int transparent,
                        cok_picture *picture)
{
    cok_picture_free(picture);
    game->error[0] = '\0';
    size_t size;
    uint8_t *data = read_record(game, name, id, &size);
    if (data == NULL) return false;
    cok_images images = {0};
    cok_image_status status = cok_images_parse(data, size, 0, &images);
    bool ok = status == COK_IMAGE_OK;
    if (!ok) {
        fail(game, "%s.DAX record %u: %s", name, id, cok_image_status_string(status));
    } else {
        cok_picture_status loaded =
            cok_picture_load(picture, images.images, images.count, transparent);
        ok = loaded == COK_PICTURE_OK;
        if (!ok)
            fail(game, "%s.DAX record %u: %s", name, id, cok_picture_status_string(loaded));
    }
    cok_images_free(&images);
    free(data);
    return ok;
}

static void moons(const cok_adventure *game, uint16_t out[3])
{
    for (size_t i = 0; i < 3; ++i) out[i] = game->vm.mem4b00[0x1f9 + i];
}

void cok_adventure_frame(cok_adventure *game)
{
    uint16_t phase[3];
    moons(game, phase);
    cok_screen_adventure(&game->screen, &game->view.tiles[4], phase);
}

/* Small pictures. */

static void free_frames(cok_adventure *game)
{
    for (size_t i = 0; i < game->frame_count; ++i) cok_picture_free(&game->frames[i]);
    game->frame_count = 0;
    game->frame = 0;
    game->picture_id = COK_ADVENTURE_NO_PICTURE;
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Load PIC<file> record id unless it is loaded (6961:00e4). Groups after the
 * first are XOR deltas; without animation only the first is kept. */
static void load_picture(cok_adventure *game, uint8_t id)
{
    if (id == game->picture_id && game->picture_file == game->vm.file) return;
    free_frames(game);
    game->error[0] = '\0';
    size_t size;
    uint8_t *data = cok_adventure_record(game, "PIC", game->vm.file, id, &size);
    if (data == NULL) return;
    game->picture_id = id;
    game->picture_file = game->vm.file;
    cok_images images = {0};
    cok_image_status status = cok_images_undelta(data, size);
    if (status == COK_IMAGE_OK) status = cok_images_parse(data, size, 1, &images);
    if (status != COK_IMAGE_OK) {
        fail(game, "PIC%u.DAX record %u: %s", game->vm.file, id, cok_image_status_string(status));
        free(data);
        return;
    }
    size_t count = game->animate ? images.count : 1;
    if (count > COK_ADVENTURE_FRAMES) count = COK_ADVENTURE_FRAMES;
    for (size_t i = 0; i < count; ++i) {
        /* Each group's delay comes just before its header. */
        game->delays[i] = u32(images.images[i].pixels - 17 - 4);
        if (cok_picture_load(&game->frames[i], &images.images[i], 1, -1) != COK_PICTURE_OK)
            break;
        game->frame_count = i + 1;
    }
    cok_images_free(&images);
    free(data);
}

/* Draw the current frame in the view (6961:000a). */
static void draw_frame(cok_adventure *game, size_t frame)
{
    if (frame < game->frame_count)
        cok_picture_draw(&game->screen, &game->frames[frame], 0, 3, 3, 0, NULL);
}

/* The big picture in its frame (6961:085b). */
static void draw_big(cok_adventure *game)
{
    uint16_t phase[3];
    moons(game, phase);
    cok_screen_big(&game->screen, &game->view.tiles[4], phase);
    if (game->big.pixels != NULL) cok_picture_draw(&game->screen, &game->big, 0, 1, 1, 0, NULL);
}

/* Draw the 3D view from the party's square (69ea:0820), under the sky that
 * 6945:00ba picks for the square. */
static void draw_view(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    /* The overhead map that 0x4bfb and DS:6d84 select is not ported. */
    cok_view_backdrop backdrop = {
        .sky = cok_view_sky_color(vm->mem4b00[vm->square < 0x80 ? 0xfd : 0xfe]),
        .horizon = 0,
        .ground = 8,
        .hour = vm->mem4b00[0xc9],
    };
    cok_view_draw(&game->screen, &game->view, vm->map_x, vm->map_y, vm->direction, &backdrop);
}

void cok_adventure_view(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (game->picture_id == 9) game->redraw = false;
    if (vm->mem4b00[0xe6] == 0 && vm->mem4b00[0x138] == 0) {
        if (game->redraw) draw_big(game);
    } else {
        vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
        draw_view(game);
    }
    game->redraw = false;
}

/* PICTURE (2fd3:0914). */
static void picture(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t id = (uint8_t)cok_ecl_value(vm, 0);
    if (id == 0xff) {
        if (!(vm->last_mode == 4 && vm->mode != 4) && game->picture_shown) {
            game->redraw = true;
            cok_adventure_view(game);
            game->picture_shown = false;
            game->view_replaced = true;
        }
        return;
    }
    game->picture_shown = true;
    if (vm->mem7c00[0x2e1] != 0xff) {
        /* 3775:0538 draws something else when 0x7ee1 is set; not ported. */
        if (game->hooks.unported != NULL) game->hooks.unported(game, game->hooks.context);
        return;
    }
    game->view_replaced = true;
    if (id < 0x70) {
        load_picture(game, id);
        if (game->picture_id != id) log_text(game, "error", game->error);
        game->frame = 0;
        draw_frame(game, 0);
        return;
    }
    /* 6961:07ed frees the small picture, then loads the big one. */
    free_frames(game);
    if (game->big_id != id || game->big.pixels == NULL) {
        game->big_id = COK_ADVENTURE_NO_PICTURE;
        char name[16];
        snprintf(name, sizeof name, "BIGPIC%u", vm->file);
        if (load_single(game, name, id, -1, &game->big))
            game->big_id = id;
        else
            log_text(game, "error", game->error);
    }
    draw_big(game);
    /* Picture 0x79 runs 4877:0005 instead, which is not ported. */
    if (id != 0x79) game->big_shown = true;
    game->redraw = false;
}

/* The 3D view's files. */

/* Load tile set set from 8X8D<file> record id, colour 13 transparent
 * (6e22:0050). */
static bool load_tiles(cok_adventure *game, unsigned set, uint8_t id)
{
    char name[16];
    snprintf(name, sizeof name, "8X8D%u", game->vm.file);
    return load_single(game, name, id, 13, &game->view.tiles[set]);
}

/* Load GEO<file> record id as the map (69ea:130d). */
static bool load_map(cok_adventure *game, uint8_t id)
{
    cok_ecl *vm = &game->vm;
    game->error[0] = '\0';
    size_t size;
    uint8_t *data = cok_adventure_record(game, "GEO", vm->file, id, &size);
    if (data == NULL) return false;
    bool ok = cok_view_set_map(&game->view, data, size);
    free(data);
    if (!ok) {
        fail(game, "GEO%u.DAX record %u is not a map", vm->file, id);
        return false;
    }
    vm->mem4b00[0xc5] = id;
    return true;
}

/* Load WALLDEF<file> record id into the wall sets from slot on, then the
 * tile sets they use (69ea:1025). Slots other than 1-3 are ignored. */
static bool load_walls(cok_adventure *game, unsigned slot, uint8_t id)
{
    if (slot < 1 || slot > 3) return true;
    cok_ecl *vm = &game->vm;
    game->error[0] = '\0';
    size_t size;
    uint8_t *data = cok_adventure_record(game, "WALLDEF", vm->file, id, &size);
    if (data == NULL) return false;
    size_t sets = cok_view_set_walls(&game->view, slot, data, size);
    free(data);
    if (sets == 0) {
        fail(game, "WALLDEF%u.DAX record %u does not fit from wall set %u", vm->file, id, slot);
        return false;
    }
    for (size_t i = 0; i < sets; ++i) {
        game->wall_ids[slot - 1 + i] = -1;
        uint8_t tiles = sets < 2 ? id : (uint8_t)(id * 10 + i + 1);
        if (!load_tiles(game, slot + (unsigned)i, tiles)) return false;
    }
    game->wall_ids[slot - 1] = id;
    return true;
}

/* LOAD FILES and LOAD PIECES (2fd3:0cf4). The original halts when a file
 * fails to load; the run stops with COK_ECL_LOAD_FAILED. */
static void load_files(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t id[4] = {0};
    for (size_t i = 1; i <= 3; ++i) id[i] = (uint8_t)cok_ecl_value(vm, i - 1);
    game->files_loaded = true;
    bool ok = true;
    if (vm->opcode == COK_ECL_LOAD_FILES) {
        game->map_loaded = true;
        if (id[1] != 0xff && id[1] != 0x7f && vm->mem4b00[0xe6] != 0) {
            ok = load_map(game, id[1]);
            vm->mem7c00[0x2c9] = 0;
        }
    } else {
        game->pieces_loaded = true;
        if (id[1] == 0x7f) {
            ok = load_walls(game, 1, 0);
        } else if (vm->mem4b00[0xe7] == 0 || vm->mem4b00[0xe8] == 0) {
            for (unsigned slot = 1; slot <= 3 && ok; ++slot) {
                if (id[slot] == 0xff)
                    game->wall_ids[slot - 1] = -1;
                else
                    ok = load_walls(game, slot, id[slot]);
            }
        } else {
            if (id[1] == 0xff)
                game->wall_ids[0] = -1;
            else
                ok = load_walls(game, 1, id[1]);
            if (id[3] == 0xff || id[3] == 0x7f)
                game->wall_ids[2] = -1;
            else if (ok)
                ok = load_walls(game, 3, id[3]);
        }
    }
    if (!ok) {
        log_text(game, "error", game->error);
        vm->status = COK_ECL_LOAD_FAILED;
        return;
    }
    if (game->pieces_loaded && game->map_loaded && vm->last_mode == 3) {
        if (vm->mode != 3 && game->frame_pending) {
            cok_adventure_frame(game);
            cok_adventure_party(game);
            cok_adventure_status(game);
        }
        game->frame_pending = false;
    }
}

/* CLEAR BOX (2fd3:3063). */
static void clear_box(cok_adventure *game)
{
    game->big_shown = false;
    cok_adventure_frame(game);
    cok_adventure_party(game);
    cok_adventure_status(game);
    draw_frame(game, 0);
    cok_adventure_status(game);
    game->frame_pending = false;
}

/* Text. */

static void page(void *context)
{
    cok_adventure *game = context;
    read_key(game); /* 1614:045c then discards pending keys. */
}

static void char_delay(void *context)
{
    cok_adventure *game = context;
    wait_ms(game, game->speed * 3u);
}

/* PRINT and PRINTCLEAR (2fd3:0acf). */
static void print(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (vm->operand[0].type < 0x80)
        snprintf(vm->string[0], sizeof vm->string[0], "%u", cok_ecl_value(vm, 0));
    bool clear = vm->opcode == COK_ECL_PRINTCLEAR;
    if (clear) vm->cursor = (cok_text_cursor){1, 0x11};
    log_text(game, "print", vm->string[0]);
    game->text_shown = true;
    cok_text_hooks hooks = {page, char_delay, game};
    cok_text_wrap(&game->screen, &game->font, &vm->cursor, vm->string[0], text_window, 10, 0,
                  clear, &hooks);
}

/* Append as much of text as fits, as a Pascal string assignment cuts it. */
static void append(char *dst, size_t size, const char *text)
{
    size_t length = strlen(dst);
    while (*text != '\0' && length + 1 < size) dst[length++] = *text++;
    dst[length] = '\0';
}

/* HORIZONTAL MENU (2fd3:116d). */
static void horizontal_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t address = (uint16_t)(vm->header[0].high << 8 | vm->header[0].low);
    size_t count = vm->operands;
    game->selected = 1;
    bool single = count == 1;
    if (single && strcmp(vm->string[0], "PRESS BUTTON OR RETURN TO CONTINUE.") == 0)
        snprintf(vm->string[0], sizeof vm->string[0], "PRESS <ENTER>/<RETURN> TO CONTINUE.");
    /* Items are ~ITEM separated by spaces, cut to 50 characters as each is
     * added. */
    char items[51] = "";
    for (size_t i = 0; i < count && i < COK_ECL_MAX_STRINGS; ++i) {
        append(items, sizeof items, "~");
        append(items, sizeof items, vm->string[i]);
        if (i + 1 < count) append(items, sizeof items, " ");
    }
    log_text(game, "menu", items);
    if (game->picture_shown && game->view_replaced) draw_frame(game, game->frame);
    cok_keyboard keys = keyboard(game);
    cok_menu_hooks hooks = {menu_special, game};
    int choice = cok_menu_horizontal(&game->screen, &game->font, "", items, 13, 15,
                                     single ? 15 : 10, single, &game->selected, &keys, &hooks);
    if (choice < 0) return;
    char text[16];
    snprintf(text, sizeof text, "%d", choice);
    log_text(game, "choice", text);
    cok_ecl_store(vm, address, (uint16_t)choice);
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
}

/* VERTICAL MENU (2fd3:0f9d): print the prompt, then list the items below
 * it. The index is stored even when the list is cancelled. */
static void vertical_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t address = (uint16_t)(vm->header[0].high << 8 | vm->header[0].low);
    char items[COK_ECL_MAX_STRINGS][41];
    const char *pointers[COK_ECL_MAX_STRINGS];
    size_t count = vm->operands < COK_ECL_MAX_STRINGS ? vm->operands : COK_ECL_MAX_STRINGS;
    for (size_t i = 0; i < count; ++i) {
        items[i][0] = '\0';
        append(items[i], sizeof items[i], vm->string[i]);
        pointers[i] = items[i];
    }
    log_text(game, "list", vm->header_string);
    game->text_shown = true;
    for (size_t i = 0; i < count; ++i) log_text(game, "item", items[i]);
    vm->cursor = (cok_text_cursor){1, 0x11};
    cok_text_hooks hooks = {page, char_delay, game};
    cok_text_wrap(&game->screen, &game->font, &vm->cursor, vm->header_string, text_window, 10,
                  0, true, &hooks);
    if (game->input_ended) return;
    cok_text_window window = {1, vm->cursor.y + 1, 0x26, 0x16};
    int index = 0;
    cok_keyboard keys = keyboard(game);
    int key = cok_menu_list(&game->screen, &game->font, pointers, count, window, 15, 10, false,
                            &index, &game->list_top, &game->selected, &keys);
    if (key < 0) return;
    char text[16];
    snprintf(text, sizeof text, "%d", index);
    log_text(game, "choice", text);
    cok_ecl_store(vm, address, (uint16_t)index);
    cok_picture_fill(&game->screen, 1, 0x11 * 8, 0x26, 6 * 8, 0);
}

/* INPUT NUMBER (2fd3:0a11) and INPUT STRING (2fd3:0a57). */
static void input(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t address = cok_ecl_address(vm, 1);
    cok_keyboard keys = keyboard(game);
    if (vm->opcode == COK_ECL_INPUT_NUMBER) {
        uint16_t number;
        if (!cok_text_number(&game->screen, &game->font, "", 10, 0, &keys, &number))
            return;
        char text[8];
        snprintf(text, sizeof text, "%u", number);
        log_text(game, "input", text);
        cok_ecl_store(vm, address, number);
        return;
    }
    char line[41];
    if (!cok_text_input(&game->screen, &game->font, "", 10, 0, 40, &keys, line)) return;
    if (line[0] == '\0') snprintf(line, sizeof line, " ");
    log_text(game, "input", line);
    cok_ecl_store_string(vm, address, line);
}

/* The party. */

void cok_adventure_party(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if ((vm->mode == 3 && vm->mem4b00[0x138] == 0) || game->big_shown) return;
    cok_party_draw(&game->screen, &game->font, &game->party, vm->character,
                   vm->mode == 0 ? 1 : 0x11, vm->mode == 5);
}

/* Str(value) cut to two characters, with a 0 before a single digit. */
static void two_digits(char out[3], unsigned value)
{
    char text[8];
    snprintf(text, sizeof text, "%u", value);
    out[0] = text[1] == '\0' ? '0' : text[0];
    out[1] = text[1] == '\0' ? text[0] : text[1];
    out[2] = '\0';
}

void cok_adventure_status(cok_adventure *game)
{
    static const char *const directions[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    cok_ecl *vm = &game->vm;
    const uint16_t *mem = vm->mem4b00;
    if (vm->mode == 3) return;
    char hours[3], minutes[3], text[64] = "";
    two_digits(hours, mem[0xc9]);
    two_digits(minutes, (uint16_t)(10 * mem[0xc8] + mem[0xc7]));
    if (mem[0xfb] == 0)
        snprintf(text, sizeof text, "%u,%u ", (uint8_t)vm->map_x, (uint8_t)vm->map_y);
    /* 0x4cff turns the names, by eighths outside 3D areas. A sum of 16 or
     * more reads past the original's table; it wraps here. */
    unsigned dir = (uint8_t)(vm->direction + (mem[0xe6] != 0 ? 2 : 1) * mem[0x1ff]);
    if (dir >= 8) dir -= 8;
    append(text, sizeof text, directions[dir % 8]);
    append(text, sizeof text, " ");
    append(text, sizeof text, hours);
    append(text, sizeof text, ":");
    append(text, sizeof text, minutes);
    /* A "*" follows while the debug flag DS:4b51 is set; it is not ported. */
    if (vm->mode == 2)
        append(text, sizeof text, " camping");
    else if ((vm->mem7c00[0x2ca] & 1) != 0)
        append(text, sizeof text, " search");
    text[40] = '\0';
    cok_picture_fill(&game->screen, 0x11, 15 * 8, 0x26 - 0x11 + 1, 8, 0); /* 1128:07e6 */
    cok_text_string(&game->screen, &game->font, text, 0x11, 15, 10, 0);
}

static uint16_t character_value(cok_ecl *vm, uint16_t address, void *context)
{
    cok_adventure *game = context;
    if (address == 0x7cc9)
        return vm->character != NULL && cok_character_former_class(vm->character);
    return (uint8_t)cok_party_index(&game->party, vm->character); /* 0x7eb1, 0x7eb4 */
}

/* A special key from a menu picks a character (546c:3334). */
static void pick_member(cok_adventure *game, uint8_t scan)
{
    game->vm.character = cok_party_special(&game->party, game->vm.character, scan);
}

/* Menus of the ECL opcodes pass special keys here, and redraw the party
 * list (3775:1885). */
static void menu_special(uint8_t scan, void *context)
{
    cok_adventure *game = context;
    pick_member(game, scan);
    cok_adventure_party(game);
}

/* Show text on row 24 in white and wait for a key (1521:096c). */
static void prompt_key(cok_adventure *game, const char *text)
{
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    cok_text_string(&game->screen, &game->font, text, 0, 24, 15, 0);
    read_key(game);
}

/* Print text in the text window in colour fg, with no delay between
 * characters (1521:04ac). */
static void print_text(cok_adventure *game, const char *text, uint8_t fg, bool clear)
{
    log_text(game, "print", text);
    game->text_shown = true;
    cok_text_hooks hooks = {page, NULL, game};
    cok_text_wrap(&game->screen, &game->font, &game->vm.cursor, text, text_window, fg, 0, clear,
                  &hooks);
}

/* A character's name, from its Pascal string. */
static void name_of(const uint8_t *c, char out[16])
{
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(out, c + 1, length);
    out[length] = '\0';
}

/* LOAD CHARACTER (2fd3:02e9): select member value & 0x7f, from 0, until
 * the script exits. Past the end of the party, the selection stays and
 * 0x7d00 reads 0. With bit 7 set, the original removes the character
 * (4def:3b0a) when DS:883a is set and its name was cleared; nothing sets
 * DS:883a, so it never does. */
static void load_character(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    vm->restore_character = true;
    uint8_t *record = cok_party_record(&game->party, cok_ecl_value(vm, 0) & 0x7f);
    if (record == NULL) {
        vm->missing_character = true;
        return;
    }
    vm->character = record;
    vm->missing_character = false;
}

/* Add experience to a character, divided among its classes (2fd3:36dc). */
static void gain_experience(cok_adventure *game, uint8_t *c, uint16_t points)
{
    uint8_t classes = 0;
    for (size_t i = 0; i < 8; ++i)
        if ((int8_t)c[0xf9 + i] > 0) ++classes;
    if (c[0x189] == 0) return;
    if (classes == 0) {
        game->vm.status = COK_ECL_DIVIDE_BY_ZERO;
        return;
    }
    uint32_t total = (uint32_t)c[0x116] | (uint32_t)c[0x117] << 8 | (uint32_t)c[0x118] << 16 |
                     (uint32_t)c[0x119] << 24;
    total += (uint16_t)(points / classes);
    for (size_t i = 0; i < 4; ++i) c[0x116 + i] = (uint8_t)(total >> (8 * i));
}

/* ADD EP (2fd3:36dc): the selected character when the first operand is 0,
 * else each member, gains the second operand's experience, divided evenly
 * among its classes, if it can act. A character with no class level is a
 * division by zero, runtime error 200 in the original. */
static void add_experience(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t points = cok_ecl_value(vm, 1);
    bool one = (uint8_t)cok_ecl_value(vm, 0) == 0;
    vm->cursor = (cok_text_cursor){1, 0x11};
    char text[64];
    if (one) {
        char name[16] = "";
        if (vm->character != NULL) name_of(vm->character, name);
        snprintf(text, sizeof text, "Congratulations %s gains experience!", name);
    } else {
        snprintf(text, sizeof text, "Congratulations the party gains experience!");
    }
    print_text(game, text, 10, true);
    wait_ms(game, game->speed * 100u); /* 521:0b4b */
    if (one) {
        if (vm->character != NULL) gain_experience(game, vm->character, points);
        return;
    }
    for (size_t i = 0; i < game->party.count && vm->status == COK_ECL_OK; ++i)
        gain_experience(game, game->party.members[i]->record, points);
}

/* Pick a character with the menu "Select" (and "Exit" with exit_item)
 * after prompt, redrawing the party list with the one picked so far
 * selected (6346:32c7). Up and down (8 and 2) move through the party,
 * wrapping; S or Enter picks. Exit, or Escape while Exit is offered,
 * picks none, but only Exit ends the menu. The original also ends it on
 * the special keys whose scan codes are 'E' and 'S' (0x45 NumLock, 0x53
 * Del). Returns the character, or NULL; *ended is set if input ended. */
static uint8_t *pick_character(cok_adventure *game, const char *prompt, uint8_t *who,
                               bool exit_item, bool *ended)
{
    cok_ecl *vm = &game->vm;
    char menu_prompt[42];
    snprintf(menu_prompt, sizeof menu_prompt, "%.40s ", prompt);
    char items[16];
    snprintf(items, sizeof items, "Select%s", exit_item ? " Exit" : "");
    *ended = false;
    int key = ' ';
    while (key != 0x0d && key != 0x1b && key != 'E' && key != 'S') {
        uint8_t *shown = vm->character;
        vm->character = who;
        cok_adventure_party(game);
        vm->character = shown;
        bool special;
        key = menu_read(game, menu_prompt, items, &special);
        if (key < 0) {
            *ended = true;
            return who;
        }
        if (!special) {
            if (exit_item && (key == 'E' || key == 0)) who = NULL;
        } else if (key == 0x50 || key == 0x48) {
            if (game->party.count == 0) {
                who = NULL;
            } else {
                size_t i = cok_party_index(&game->party, who), n = game->party.count;
                /* The original takes the next of a NULL pick, and loops
                 * forever looking for the one before a non-member. */
                if (i == n) i = 0;
                who = cok_party_record(&game->party, key == 0x50 ? (i + 1) % n : (i + n - 1) % n);
            }
        }
    }
    return who;
}

/* WHO (2fd3:30b6): clear the text window and pick the selected character
 * with the string operand as the prompt. */
static void who(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    bool moving = game->moving;
    game->moving = false;
    vm->missing_character = false;
    cok_picture_fill(&game->screen, 1, 0x11 * 8, 0x26, 6 * 8, 0); /* 1521:0b60 */
    bool ended;
    uint8_t *picked = pick_character(game, vm->string[0], vm->character, false, &ended);
    vm->character = picked;
    game->moving = moving;
    if (ended) return;
    char name[16] = "";
    if (picked != NULL) name_of(picked, name);
    log_text(game, "who", name);
}

/* Sum count rolls of 1 to sides, as a byte (60f4:1216). */
static uint8_t roll(cok_adventure *game, uint8_t count, uint8_t sides)
{
    uint8_t sum = 0;
    for (unsigned i = 0; i < count; ++i)
        sum = (uint8_t)(sum + cok_tp_random(&game->vm.seed, sides) + 1);
    return sum;
}

/* The party member whose record is c, or NULL. */
static cok_character *member_of(cok_adventure *game, const uint8_t *c)
{
    size_t i = cok_party_index(&game->party, c);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* A spell effect could not be carried out: say why and end the run. */
static void effect_failed(cok_adventure *game)
{
    fail(game, "%s", game->effects.error);
    log_text(game, "error", game->error);
    game->vm.status = COK_ECL_EFFECT_FAILED;
    game->vm.abort = true;
}

/* Whether an attack with bonus hits member c (60f4:0ffb, see effect.h). */
static bool attack_hits(cok_adventure *game, uint8_t *c, uint8_t bonus, bool *hit)
{
    if (cok_effects_attack(&game->effects, member_of(game, c), bonus, hit)) return true;
    effect_failed(game);
    return false;
}

/* Whether member c makes saving throw type with bonus (60f4:113a, see
 * effect.h). */
static bool save_made(cok_adventure *game, uint8_t *c, uint8_t type, uint8_t bonus, bool *made)
{
    if (cok_effects_save(&game->effects, member_of(game, c), type, bonus, made)) return true;
    effect_failed(game);
    return false;
}

/* Damage character c and say so in the text window, a page at a time, then
 * redraw the party list (3775:20a6). The dead take none. */
static void apply_damage(cok_adventure *game, uint8_t *c, uint16_t damage)
{
    cok_ecl *vm = &game->vm;
    if (c[0x188] == 6) return;
    char name[16], text[80];
    name_of(c, name);
    if (c[0x197] + 10 < damage) {
        snprintf(text, sizeof text, "  %s dies. ", name);
    } else {
        char points[8];
        snprintf(points, sizeof points, "%u", damage);
        points[3] = '\0'; /* string[3] */
        snprintf(text, sizeof text, "  %s is hit FOR %s points of Damage.", name, points);
    }
    bool clear = false;
    if (vm->cursor.y > 0x16) {
        vm->cursor.y = 0x11;
        clear = true;
        prompt_key(game, "press <enter>/<return> to continue");
    }
    vm->cursor.x = 0x26;
    print_text(game, text, 15, clear);
    /* The original passes the damage as a byte, so the message can say
     * "dies" for 256 or more while the character takes less. */
    cok_character_damage(c, (uint8_t)damage);
    cok_picture_fill(&game->screen, 0x11, 8, 0x26 - 0x11 + 1, 15 * 8, 0); /* 1521:0b60 */
    cok_adventure_party(game);
}

/* The member a roll of 1 to the party's size picks; NULL past the end of
 * the party, where the original reads through a NULL next pointer. */
static uint8_t *member_rolled(cok_adventure *game, uint8_t rolled)
{
    return cok_party_record(&game->party, rolled == 0 ? 0 : rolled - 1u);
}

/* DAMAGE (2fd3:2c80) deals the fourth operand plus the second operand's
 * rolls of 1 to the third. Without bit 7 of the first operand, that many
 * attacks each hit a random member if they beat its armour class with the
 * fifth operand as bonus, rolling new damage after each. With bit 7, the
 * low five bits are a saving throw bonus and the fifth operand's low three
 * bits the throw: with bit 6 every member takes the damage unless it saves
 * (or always, with bit 5), with fifth operand bit 7 the selected character
 * (throw type - 1, none for type 0), and otherwise a random member. Bit 4
 * deals it even when the save is made. If no member can act afterwards,
 * the party is killed and the run ends. Members are rolled from the size in
 * 0x7f3e. The members' spell effects change the attack and saving rolls;
 * one that is not ported stops the run with COK_ECL_EFFECT_FAILED. */
static void damage(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t *saved = vm->character;
    uint8_t op[5];
    for (size_t i = 0; i < 5; ++i) op[i] = (uint8_t)cok_ecl_value(vm, i);
    uint8_t size = (uint8_t)vm->mem7c00[0x33e];
    uint16_t amount = (uint16_t)(op[3] + roll(game, op[1], op[2]));
    uint8_t target = 0;
    if ((op[0] & 0x40) == 0) target = roll(game, 1, size);
    bool ok = true, hit, made;
    if ((op[0] & 0x80) == 0) {
        for (unsigned k = 1; k <= op[0] && ok; ++k) {
            uint8_t *c = member_rolled(game, roll(game, 1, size));
            if (c != NULL && (ok = attack_hits(game, c, op[4], &hit)) && hit)
                apply_damage(game, c, amount);
            amount = (uint16_t)(op[3] + roll(game, op[1], op[2]));
        }
    } else {
        uint8_t bonus = op[0] & 0x1f, type = op[4] & 7;
        bool always = (op[0] & 0x10) != 0;
        if ((op[0] & 0x40) != 0) {
            for (size_t i = 0; i < game->party.count && ok; ++i) {
                uint8_t *c = game->party.members[i]->record;
                made = false;
                if ((op[0] & 0x20) == 0) ok = save_made(game, c, type, bonus, &made);
                if (ok && (!made || always)) apply_damage(game, c, amount);
            }
        } else if ((op[4] & 0x80) != 0) {
            uint8_t *c = vm->character;
            made = false;
            if (c != NULL && type != 0) ok = save_made(game, c, (uint8_t)(type - 1), bonus, &made);
            if (c != NULL && ok && (!made || always)) apply_damage(game, c, amount);
        } else {
            uint8_t *c = member_rolled(game, target);
            if (c != NULL && (ok = save_made(game, c, type, bonus, &made)) && (!made || always))
                apply_damage(game, c, amount);
        }
    }
    if (!ok) {
        vm->character = saved;
        return;
    }
    bool alive = false;
    for (size_t i = 0; i < game->party.count; ++i)
        if (game->party.members[i]->record[0x189] != 0) alive = true;
    game->party_killed = !alive;
    /* The original sets DS:4b57, which ends the run, to whether the party
     * died; here it also stays set when input has ended. */
    vm->abort = !alive || game->input_ended;
    if (!alive) {
        uint16_t phase[3];
        moons(game, phase);
        cok_screen_frame(&game->screen, &game->view.tiles[4], phase, true); /* 1128:0000 */
        vm->cursor = (cok_text_cursor){2, 2};
        const char *text = "The entire party is killed!";
        log_text(game, "print", text);
        cok_text_hooks hooks = {page, NULL, game};
        cok_text_wrap(&game->screen, &game->font, &vm->cursor, text,
                      (cok_text_window){1, 1, 0x26, 0x16}, 10, 0, true, &hooks);
        wait_ms(game, 3000);
    }
    vm->character = saved;
    prompt_key(game, "press <enter>/<return> to continue");
}

/* Reset picture state for a new block, as 3775:01e8 does (DS:884a, 884c,
 * 8830). */
static void reset_pictures(cok_adventure *game)
{
    game->picture_shown = false;
}

static void opcode(cok_ecl *vm, void *context)
{
    cok_adventure *game = context;
    switch (vm->opcode) {
    case COK_ECL_EXIT: case COK_ECL_RETURN:
        /* The original clears DS:8830, 884a, 884c and 8848 here. */
        game->picture_shown = false;
        break;
    case COK_ECL_NEWECL: reset_pictures(game); break;
    case COK_ECL_PRINT: case COK_ECL_PRINTCLEAR: print(game); break;
    case COK_ECL_PRINT_RETURN: break;
    case COK_ECL_HORIZONTAL_MENU: horizontal_menu(game); break;
    case COK_ECL_VERTICAL_MENU: vertical_menu(game); break;
    case COK_ECL_INPUT_NUMBER: case COK_ECL_INPUT_STRING: input(game); break;
    case COK_ECL_PICTURE: picture(game); break;
    case COK_ECL_CLEAR_BOX: clear_box(game); break;
    case COK_ECL_DELAY: wait_ms(game, game->speed * 100u); break;
    case COK_ECL_LOAD_FILES: case COK_ECL_LOAD_PIECES: load_files(game); break;
    case COK_ECL_LOAD_CHARACTER: load_character(game); break;
    case COK_ECL_ADD_EP: add_experience(game); break;
    case COK_ECL_WHO: who(game); break;
    case COK_ECL_DAMAGE: damage(game); break;
    default:
        if (game->hooks.unported != NULL) game->hooks.unported(game, game->hooks.context);
        break;
    }
}

static bool load_block(cok_ecl *vm, uint8_t block, void *context)
{
    cok_adventure *game = context;
    game->error[0] = '\0';
    size_t size;
    uint8_t *data = cok_adventure_record(game, "ECL", vm->file, block, &size);
    if (data == NULL) return false;
    cok_ecl_status status = cok_ecl_load(vm, data, size);
    free(data);
    if (status != COK_ECL_OK) {
        fail(game, "ECL%u.DAX record %u: %s", vm->file, block, cok_ecl_status_string(status));
        return false;
    }
    /* Blocks 0 and 0x50 have no squares off the map (DS:8846). */
    game->view.wrap = block != 0 && block != 0x50;
    return true;
}

static void trace(cok_ecl *vm, void *context)
{
    (void)vm;
    cok_adventure *game = context;
    if (game->hooks.trace != NULL) game->hooks.trace(game, game->hooks.context);
}

bool cok_adventure_open(cok_adventure *game, const char *assets, const cok_keyboard *keys,
                        const cok_adventure_hooks *hooks)
{
    memset(game, 0, sizeof *game);
    cok_ecl_hooks vm_hooks = {.load = load_block, .opcode = opcode, .trace = trace,
                              .character_value = character_value, .context = game};
    cok_ecl_init(&game->vm, &vm_hooks);
    cok_effects_init(&game->effects, &game->vm, &game->party, &game->item_types);
    game->vm.file = 1;
    game->picture_id = COK_ADVENTURE_NO_PICTURE;
    game->big_id = COK_ADVENTURE_NO_PICTURE;
    game->speed = 4;
    for (size_t i = 0; i < 3; ++i) game->wall_ids[i] = -1;
    game->animate = true;
    game->selected = 1;
    game->text_shown = true;
    if (keys != NULL) game->keys = *keys;
    if (hooks != NULL) game->hooks = *hooks;
    if (strlen(assets) >= sizeof game->assets) {
        fail(game, "asset path too long");
        return false;
    }
    snprintf(game->assets, sizeof game->assets, "%s", assets);
    if (cok_picture_create(&game->screen, 40, 200, 1, 0) != COK_PICTURE_OK) {
        fail(game, "out of memory");
        return false;
    }
    size_t size;
    uint8_t *data = cok_adventure_record(game, "8X8D", 1, 201, &size);
    if (data == NULL) return false;
    cok_picture_status status = cok_font_load(&game->font, data, size);
    free(data);
    if (status != COK_PICTURE_OK) {
        fail(game, "8X8D1.DAX record 201: %s", cok_picture_status_string(status));
        return false;
    }
    /* The game loads these once at startup, while the ECL file is still 1
     * (3e99:005b): the frame's tile set, tile set 0, and outside CGA mode
     * the sky pictures. */
    if (!load_tiles(game, 4, 202) || !load_tiles(game, 0, 203)) return false;
    for (uint8_t i = 0; i < COK_VIEW_SKY_PICTURES; ++i)
        if (!load_single(game, "SKY", (uint8_t)(250 + i), 13, &game->view.sky[i])) return false;
    /* 3e99:005b also reads the item types. */
    char path[sizeof game->assets + 32];
    snprintf(path, sizeof path, "%s/ITEMS", game->assets);
    return cok_item_types_read(path, &game->item_types, game->error, sizeof game->error);
}

void cok_adventure_close(cok_adventure *game)
{
    cok_party_free(&game->party);
    cok_effects_free(&game->effects);
    game->vm.character = NULL;
    free_frames(game);
    cok_picture_free(&game->big);
    cok_view_free(&game->view);
    cok_picture_free(&game->screen);
    cok_font_free(&game->font);
}

cok_ecl_status cok_adventure_load(cok_adventure *game, uint8_t block)
{
    cok_ecl *vm = &game->vm;
    vm->block = block;
    if (!load_block(vm, block, game)) return vm->status = COK_ECL_LOAD_FAILED;
    reset_pictures(game);
    return cok_ecl_start(vm, !vm->keep_vars);
}

/* Saved games. */

/* The directory part of path, or "." for none. */
static void directory_of(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL)
        snprintf(out, size, ".");
    else
        snprintf(out, size, "%.*s", (int)(slash == path ? 1 : slash - path), path);
}

/* Add the characters a saved game names, from its directory. */
static bool add_characters(cok_adventure *game, const cok_saved_game *saved, const char *path)
{
    cok_ecl *vm = &game->vm;
    char dir[4096];
    directory_of(path, dir, sizeof dir);
    for (size_t i = 0; i < saved->count && i < COK_PARTY_MAX; ++i) {
        char base[9];
        cok_party_file_name(saved->names[i], base);
        char file[4200];
        snprintf(file, sizeof file, "%s/%s.SAV", dir, base);
        FILE *exists = fopen(file, "rb");
        if (exists == NULL) continue; /* the original skips missing characters */
        fclose(exists);
        cok_character *character = malloc(sizeof *character);
        if (character == NULL) {
            fail(game, "out of memory");
            return false;
        }
        if (!cok_character_read(character, dir, base, &game->item_types, game->error,
                                sizeof game->error)) {
            cok_character_free(character);
            free(character);
            return false;
        }
        if (!cok_party_add(&game->party, character)) {
            cok_character_free(character);
            free(character);
            fail(game, "%s: the party is full", file);
            return false;
        }
        /* 4b6d:1989 recomputes an NPC's levels once it has joined. */
        if (character->record[0xe7] >= 0x80 &&
            !cok_character_levels(character, &game->item_types, game->error, sizeof game->error))
            return false;
        ++vm->mem7c00[0x33e];
    }
    /* Adding a character selects it; the loader then selects the first. */
    vm->character = cok_party_record(&game->party, 0);
    return true;
}

bool cok_adventure_load_party(cok_adventure *game, const char *path)
{
    static cok_saved_game saved;
    game->error[0] = '\0';
    if (!cok_saved_game_read(path, &saved, game->error, sizeof game->error)) return false;
    return add_characters(game, &saved, path);
}

bool cok_adventure_restore(cok_adventure *game, const char *path)
{
    static cok_saved_game saved;
    cok_ecl *vm = &game->vm;
    game->error[0] = '\0';
    if (!cok_saved_game_read(path, &saved, game->error, sizeof game->error)) return false;
    vm->keep_vars = true;
    memcpy(vm->mem4b00, saved.mem4b00, sizeof vm->mem4b00);
    memcpy(vm->mem7c00, saved.mem7c00, sizeof vm->mem7c00);
    memcpy(vm->mem7a00, saved.mem7a00, sizeof vm->mem7a00);
    vm->map_x = saved.map_x;
    vm->map_y = saved.map_y;
    vm->direction = saved.direction;
    vm->ahead = saved.ahead;
    vm->square = saved.square;
    game->animate = (vm->mem4b00[0xff] & 1) != 0; /* DS:4b4f; bit 1 and up are DS:4b4d */
    game->speed = (uint8_t)vm->mem4b00[0xfc];
    vm->mem7c00[0x33e] = 0;
    if (!add_characters(game, &saved, path)) return false;
    /* The save holds the ECL file twice; the loader takes it from 0x7f12. */
    vm->file = (uint8_t)vm->mem7c00[0x312];
    if (vm->mem4b00[0xe6] != 0) {
        /* The map reloads only if wall set 1's record is above 0. */
        if (saved.wall_ids[0] > 0 && !load_map(game, (uint8_t)vm->mem4b00[0xc5])) return false;
        for (size_t i = 0; i < 3; ++i) {
            if (saved.wall_ids[i] <= 0) continue;
            if (!load_walls(game, (unsigned)saved.wall_slots[i], (uint8_t)saved.wall_ids[i]))
                return false;
        }
    }
    vm->last_mode = saved.mode;
    vm->mode = 0; /* the party menu */
    return true;
}

/* Enter the loaded block (2fd3:3b47): run its load vector, show the view if
 * it loaded files or stays in 3D, then run the after-move and location
 * vectors, starting over whenever NEWECL switches blocks. */
static cok_ecl_status enter_block(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_status status = COK_ECL_OK;
    do {
        free_frames(game);
        vm->reload = false;
        vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
        vm->mem7c00[0x2d5] = 0;
        vm->saved_character = vm->character; /* DS:43bf */
        status = cok_ecl_run(vm, vm->vectors[4]);
        if (status != COK_ECL_OK || vm->abort || vm->reload) continue;
        vm->mem4b00[0xf2] = vm->block;
        if (((vm->last_mode != 4 || vm->mode == 4) && game->files_loaded) ||
            (vm->last_mode == 4 && vm->mode == 4))
            cok_adventure_view(game);
        status = cok_ecl_run(vm, vm->vectors[0]);
        if (status != COK_ECL_OK || vm->abort || vm->reload) continue;
        status = cok_ecl_run(vm, vm->vectors[1]);
        if (status != COK_ECL_OK || vm->abort || vm->reload) continue;
        vm->character = vm->saved_character;
        cok_adventure_party(game);
    } while (status == COK_ECL_OK && !vm->abort && vm->reload);
    vm->last_mode = vm->mode;
    return status;
}

cok_ecl_status cok_adventure_enter(cok_adventure *game, uint8_t block)
{
    cok_ecl *vm = &game->vm;
    /* As 2fd3:3c28 starts. */
    game->redraw = true;
    game->files_loaded = game->pieces_loaded = game->map_loaded = false;
    game->frame_pending = vm->mem4b00[0xf2] != 0;
    vm->saved_character = vm->character;
    vm->mode = 4;
    if (vm->mem4b00[0xf2] == 0) cok_adventure_party(game); /* starting block 0x24 */
    if (vm->mem4b00[0xe6] == 0) vm->mode = 3;
    cok_ecl_status status = cok_adventure_load(game, block);
    return status == COK_ECL_OK ? enter_block(game) : status;
}

/* The game clock. */

/* Advance moon i's days in its phase (0x4cfc-0x4cfe), and once they reach
 * days, its phase (0x4cf9-0x4cfb), redrawing it on the frame (57e4:001e). */
static void moon(cok_adventure *game, unsigned i, uint16_t days)
{
    static const uint8_t column[3] = {8, 19, 30}, tile[3] = {10, 6, 14};
    uint16_t *mem = game->vm.mem4b00;
    if (mem[0x1fc + i] < days) {
        ++mem[0x1fc + i];
        return;
    }
    mem[0x1f9 + i] = mem[0x1f9 + i] < 3 ? mem[0x1f9 + i] + 1 : 0;
    cok_view_tile(&game->screen, &game->view, 0x114u + ((mem[0x1f9 + i] + tile[i]) & 0xff),
                  column[i], 0, false);
    mem[0x1fc + i] = 0;
}

/* Carry each unit that is full into the next, once (57e4:0459). */
static void carry(cok_adventure *game, uint16_t clock[7])
{
    for (size_t i = 0; i < 7; ++i) {
        if (clock[i] < cok_clock_units[i]) continue;
        if (i == 6) {
            /* A full last unit, 256 years, stays full and ages each
             * character a year (the word at +0x60) every time a unit
             * passes. Months carry into years without aging anyone. */
            for (size_t k = 0; k < game->party.count; ++k) {
                uint8_t *c = game->party.members[k]->record;
                uint16_t age = (uint16_t)(c[0x60] | c[0x61] << 8);
                ++age;
                c[0x60] = (uint8_t)age;
                c[0x61] = (uint8_t)(age >> 8);
            }
            continue;
        }
        ++clock[i + 1];
        clock[i] -= cok_clock_units[i];
        if (i == 3) {
            moon(game, 0, 8);
            moon(game, 1, 1);
            moon(game, 2, 6);
        }
    }
}

bool cok_adventure_pass_time(cok_adventure *game, unsigned unit, unsigned count)
{
    if (unit > 6) return true;
    uint16_t clock[7];
    memcpy(clock, &game->vm.mem4b00[0xc6], sizeof clock);
    for (unsigned i = 0; i < count; ++i) {
        ++clock[unit];
        carry(game, clock);
    }
    memcpy(&game->vm.mem4b00[0xc6], clock, sizeof clock);
    if (cok_effects_pass_time(&game->effects, unit, count)) return true;
    effect_failed(game);
    return false;
}

/* The adventure loop. */

static void unported_command(cok_adventure *game, const char *what)
{
    log_text(game, "unported", what);
}

static void log_position(cok_adventure *game)
{
    char text[32];
    snprintf(text, sizeof text, "%d,%d,%u", game->vm.map_x, game->vm.map_y, game->vm.direction);
    log_text(game, "at", text);
}

/* One key from a menu of the adventure loop (67b5:03e2): prompt in 13,
 * items in 15 and 10. */
static int menu_read(cok_adventure *game, const char *prompt, const char *items, bool *special)
{
    log_text(game, "menu", items);
    cok_keyboard keys = keyboard(game);
    return cok_menu_read(&game->screen, &game->font, prompt, items, 13, 15, 10, &game->selected,
                         &keys, special);
}

/* Turn by eighths of a full turn and redraw the view (475c:09ec). */
static void turn(cok_adventure *game, unsigned by)
{
    cok_ecl *vm = &game->vm;
    /* Turning left or right first plays sound 10 (DS:1e5c); sound is not
     * ported. */
    vm->direction = (uint8_t)((vm->direction + by) % 8);
    vm->ahead = cok_view_wall(&game->view, vm->direction, vm->map_x, vm->map_y);
    log_position(game);
    draw_view(game);
    cok_adventure_status(game);
}

/* Mark a step off the map in 0x7ed5 before the after-move vector runs
 * (475c:0765). */
static void check_step(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    vm->mem7c00[0x2d5] = 0;
    if (cok_view_passage(&game->view, vm->direction, vm->map_x, vm->map_y) == 0) return;
    int x = vm->map_x, y = vm->map_y;
    cok_view_step(vm->direction, &x, &y);
    if (x > 15 || x < 0) {
        vm->map_x = x > 15 ? 15 : 0;
        vm->mem7c00[0x2d5] = 1;
    }
    if (y > 15 || y < 0) {
        vm->map_y = y > 15 ? 15 : 0;
        vm->mem7c00[0x2d5] = 1;
    }
}

/* Take a command from the adventure menu (475c:09ec in a 3D area). Returns
 * 0 for a step forward, 'E' to camp, 'L' to look, or -1 if input ended. */
static int command(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    vm->mem7c00[0x2c9] = 0;
    int result = 0;
    for (bool done = false; !done;) {
        bool special;
        int key;
        if (!game->moving) {
            key = menu_read(game, "", "Move Area Cast View Encamp Search Look", &special);
            if (key < 0) return -1;
            result = key;
            if (special) {
                pick_member(game, (uint8_t)key);
                cok_adventure_party(game);
                cok_adventure_status(game);
                continue;
            }
            switch (key) {
            case 'M': game->moving = true; break;
            case 'A':
                /* The overhead map (69ea:000f), or "Not Here". */
                unported_command(game, "Area");
                break;
            case 'C':
                if (vm->character == NULL || vm->character[0x188] == 0) {
                    game->selected = 1;
                    unported_command(game, "Cast");
                }
                break;
            case 'V':
                game->selected = 1;
                unported_command(game, "View");
                break;
            case 'E':
                game->selected = 1;
                done = true;
                break;
            case 'S':
                vm->mem7c00[0x2ca] ^= 1;
                cok_adventure_status(game);
                break;
            case 'L':
                vm->mem7c00[0x2ca] |= 2;
                cok_adventure_status(game);
                cok_adventure_pass_time(game, 2, 1);
                done = true;
                break;
            default: break;
            }
            continue;
        }
        key = menu_read(game, "", "Exit", &special);
        if (key < 0) return -1;
        result = key;
        if (!special) {
            if (key == 'E') game->moving = false;
            continue;
        }
        switch (key) {
        case 0x48:
            check_step(game);
            cok_adventure_status(game);
            done = true;
            result = 0;
            break;
        case 0x50: turn(game, 4); result = 0; break;
        case 0x4b: turn(game, 6); result = 0; break;
        case 0x4d: turn(game, 2); result = 0; break;
        default: break;
        }
    }
    if (game->text_shown) {
        cok_picture_fill(&game->screen, 1, 0x11 * 8, 0x26, 6 * 8, 0); /* 1128:07e6 */
        game->text_shown = false;
    }
    return result;
}

/* Step ahead, wrapping at the map's edges, and pass a minute, or ten while
 * searching (475c:0813). */
static void advance(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    /* Sound 10 (DS:1e5c) plays first; sound is not ported. */
    wait_ms(game, 50);
    int x = vm->map_x, y = vm->map_y;
    cok_view_step(vm->direction, &x, &y);
    vm->map_x = (int8_t)(x < 0 ? 15 : x > 15 ? 0 : x);
    vm->map_y = (int8_t)(y < 0 ? 15 : y > 15 ? 0 : y);
    vm->ahead = cok_view_wall(&game->view, vm->direction, vm->map_x, vm->map_y);
    for (size_t i = 0; i < 3; ++i) game->door_tries[i] = true;
    vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
    log_position(game);
    cok_adventure_pass_time(game, (vm->mem7c00[0x2ca] & 1) != 0 ? 2 : 1, 1);
}

/* Open the side of the party's square it faces, and the facing side of the
 * square ahead (475c:0148). */
static void open_door(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    int x = vm->map_x, y = vm->map_y;
    cok_view_open(&game->view, vm->direction, x, y);
    cok_view_step(vm->direction, &x, &y);
    cok_view_open(&game->view, (vm->direction + 4u) % 8, x, y);
}

/* Bash (475c:02f3): each member in turn rolls against its strength (+0x11)
 * and exceptional strength (+0x1c) until one breaks the door open, which
 * is harder for a door that cannot be picked (passage 3). A member too weak
 * to try stops Bash being offered after this. */
static bool bash(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    bool opened = false;
    for (size_t i = 0; i < game->party.count && !opened; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        uint8_t strength = c[0x11], extra = c[0x1c];
        if (cok_view_passage(&game->view, vm->direction, vm->map_x, vm->map_y) == 3) {
            if (strength == 18) {
                if (extra >= 0x5b && extra <= 99) opened = roll(game, 1, 6) == 1;
                else if (extra == 100) opened = roll(game, 1, 6) < 3;
                else game->door_tries[0] = false;
            } else if (strength == 19 || strength == 20) {
                opened = roll(game, 1, 6) < 4;
            } else if (strength == 21 || strength == 22) {
                opened = roll(game, 1, 6) < 5;
            } else if (strength == 23) {
                opened = roll(game, 1, 6) < 6;
            } else if (strength == 24) {
                opened = roll(game, 1, 8) < 8;
            } else if (strength == 25) {
                opened = true;
            } else {
                game->door_tries[0] = false;
            }
        } else if (strength >= 3 && strength <= 7) {
            opened = roll(game, 1, 6) == 1;
        } else if (strength >= 8 && strength <= 15) {
            opened = roll(game, 1, 6) < 3;
        } else if (strength == 16 || strength == 17) {
            opened = roll(game, 1, 6) < 4;
        } else if (strength == 18) {
            if (extra < 0x33) opened = roll(game, 1, 6) < 4;
            else if (extra <= 99) opened = roll(game, 1, 6) < 5;
            else if (extra == 100) opened = roll(game, 1, 6) < 6;
        } else if (strength == 19 || strength == 20) {
            opened = roll(game, 1, 8) < 8;
        } else if (strength == 21) {
            opened = roll(game, 1, 10) < 10;
        } else if (strength == 22 || strength == 23) {
            opened = roll(game, 1, 12) < 12;
        } else if (strength == 24) {
            opened = roll(game, 1, 20) < 20;
        } else if (strength == 25) {
            opened = true;
        }
    }
    if (opened) open_door(game);
    return opened;
}

/* Whether a member has a level in class (+0xf9), or had one before
 * changing class (+0x101) and may use it (475c:0275). */
static bool has_class(cok_adventure *game, size_t class)
{
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        if ((int8_t)c[0xf9 + class] >= 1) return true;
        if ((int8_t)c[0x101 + class] > 0 && cok_character_former_class(c)) return true;
    }
    return false;
}

/* Pick (475c:05b6): each member rolls 1-100 against its lock picking
 * (+0xdc) until one who is okay succeeds. It is not offered again until
 * the next step. */
static bool pick(cok_adventure *game)
{
    bool opened = false;
    for (size_t i = 0; i < game->party.count && !opened; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        uint8_t r = roll(game, 1, 100);
        opened = r <= c[0xdc] && c[0x188] == 0;
    }
    game->door_tries[1] = false;
    if (opened) open_door(game);
    return opened;
}

/* The first member who has memorized spell, and where (475c:06c6,
 * 475c:0683): the 58 bytes from +0x1e. */
static uint8_t *memorized(cok_adventure *game, uint8_t spell)
{
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *c = game->party.members[i]->record;
        for (size_t s = 0; s < 0x3a; ++s)
            if (c[0x1e + s] == spell) return c + 0x1e + s;
    }
    return NULL;
}

/* Knock (475c:0720): the first member who has memorized spell 0x1f casts
 * it, and the party passes the door once; the door stays locked. */
static bool knock(cok_adventure *game)
{
    uint8_t *spell = memorized(game, 0x1f);
    if (spell == NULL) return false;
    *spell = 0;
    return true;
}

/* Offer the ways to open a locked door (475c:0e77). Returns true if the
 * party gets through. Bash is offered until a member is too weak to try,
 * Pick if a member is a thief (class 6) and Knock if one has memorized
 * spell 0x1f; all three again after each step, though before the first
 * step none are. With none to offer, no menu shows. Choosing Pick at a
 * door that cannot be picked stops it being offered until the next step.
 * As in the original, special keys count as the letter of their scan
 * code, so the down arrow picks and the left arrow knocks. */
static bool locked_door(cok_adventure *game, uint8_t passage)
{
    char items[41] = "";
    if (game->door_tries[0]) append(items, sizeof items, "Bash");
    if (game->door_tries[1] && has_class(game, 6)) append(items, sizeof items, " Pick");
    if (game->door_tries[2] && memorized(game, 0x1f) != NULL) append(items, sizeof items, " Knock");
    append(items, sizeof items, " Exit");
    if (strcmp(items, " Exit") == 0) return false;
    bool special;
    int key = menu_read(game, "Locked. ", items, &special);
    if (key < 0) return false;
    if (key == 'B') return bash(game);
    if (key == 'P') {
        if (passage == 2) return pick(game);
        game->door_tries[1] = false;
    }
    if (key == 'K') return knock(game);
    return false;
}

/* Take the step chosen, unless the after-move vector set 0x7ec9 to 0xff
 * (475c:0e77 in a 3D area). */
static void step(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (vm->mem7c00[0x2c9] < 0xff) {
        game->redraw = true;
        uint8_t passage = cok_view_passage(&game->view, vm->direction, vm->map_x, vm->map_y);
        bool moved = passage == 1;
        if (passage == 2 || passage == 3) moved = locked_door(game, passage);
        if (moved) advance(game);
        cok_adventure_status(game);
    } else {
        vm->mem7c00[0x2c9] = 0;
    }
    free_frames(game); /* 6961:0537 */
}

/* Camp (2fd3:3403): run the camp vector, then the camp menu (4888:2c31),
 * which is not ported, so the party never rests. */
static cok_ecl_status camp(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_status status = cok_ecl_run(vm, vm->vectors[2]);
    if (status != COK_ECL_OK || vm->abort) return status;
    unported_command(game, "Encamp");
    game->effects.rolls.saved = 0; /* DS:5885 */
    game->redraw = true;
    if (vm->mem4b00[0x138] == 0) cok_adventure_view(game);
    return COK_ECL_OK;
}

/* Look (2fd3:3c28): run the location vector once as if searching. */
static cok_ecl_status look(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t searching = vm->mem7c00[0x2ca] & 1;
    vm->mem7c00[0x2ca] = 1;
    game->redraw = true;
    cok_adventure_view(game);
    cok_ecl_status status = cok_ecl_run(vm, vm->vectors[1]);
    if (status == COK_ECL_OK && !vm->abort && vm->reload) status = enter_block(game);
    vm->mem7c00[0x2ca] = searching;
    return status;
}

/* Redraw the screen for the mode (6346:2c17): in a 3D area the frame, the
 * view unless 0x4c38 is set, the party list and the status line. Only the
 * 3D and plain area modes are ported. */
static void redraw_screen(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    game->redraw = true;
    if (vm->mode == 4) {
        cok_adventure_frame(game);
        if (vm->mem4b00[0x138] == 0) cok_adventure_view(game);
        cok_adventure_party(game);
        cok_adventure_status(game);
        game->frame_pending = false;
    } else if (vm->mode == 3 && game->picture_id != 9 && vm->mem4b00[0x138] == 0) {
        cok_adventure_view(game);
    }
}

/* Take a command and keep the character selected after it, which EXIT
 * restores after LOAD CHARACTER (DS:43bf). */
static int next_command(cok_adventure *game)
{
    int key = command(game);
    game->vm.saved_character = game->vm.character;
    return key;
}

cok_ecl_status cok_adventure_play(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_status status = COK_ECL_OK;
    /* A block entered from a saved game redraws the whole screen. */
    if (vm->mode != 3 && vm->keep_vars) {
        if (game->frame_pending) redraw_screen(game);
        game->redraw = true;
        cok_adventure_view(game);
    }
    vm->keep_vars = false;
    game->moving = false;
    while (status == COK_ECL_OK && !vm->abort) {
        if (vm->mode != 4) {
            /* 475c:09ec and 475c:08d5 move the party on the overland map. */
            unported_command(game, "travel outside 3D areas");
            break;
        }
        int key = next_command(game);
        if (key < 0) break;
        if (!vm->reload) vm->mem4b00[0xf2] = vm->block;
        while (status == COK_ECL_OK && !vm->abort && (vm->mem7c00[0x2ca] > 1 || key == 'E')) {
            status = key == 'E' ? camp(game) : look(game);
            if (status == COK_ECL_OK && !vm->abort) key = next_command(game);
        }
        if (status != COK_ECL_OK || vm->abort) break;
        status = cok_ecl_run(vm, vm->vectors[0]);
        if (status != COK_ECL_OK || vm->abort) break;
        if (vm->reload) {
            status = enter_block(game);
            continue;
        }
        vm->mem4b00[0xf0] = (uint16_t)vm->map_x;
        vm->mem4b00[0xf1] = (uint16_t)vm->map_y;
        step(game);
        if (vm->status == COK_ECL_EFFECT_FAILED) break; /* the effect timers */
        cok_adventure_view(game);
        /* Sound 10 plays if the party moved. */
        game->picture_shown = false;
        game->view_replaced = true;
        status = cok_ecl_run(vm, vm->vectors[1]);
        if (status == COK_ECL_OK && !vm->abort && vm->reload) status = enter_block(game);
    }
    /* The effect timers fail between runs (cok_adventure_pass_time). */
    if (status == COK_ECL_OK && vm->status == COK_ECL_EFFECT_FAILED) status = vm->status;
    vm->abort = false;
    return status;
}
