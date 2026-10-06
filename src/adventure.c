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

uint8_t *cok_adventure_record(cok_adventure *game, const char *name, unsigned file,
                              uint8_t id, size_t *size)
{
    char path[sizeof game->assets + 32];
    snprintf(path, sizeof path, "%s/%s%u.DAX", game->assets, name, file);
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

/* Load a record of one image or one group of frames into picture. */
static bool load_single(cok_adventure *game, const char *name, unsigned file, uint8_t id,
                        cok_picture *picture)
{
    game->error[0] = '\0';
    size_t size;
    uint8_t *data = cok_adventure_record(game, name, file, id, &size);
    if (data == NULL) return false;
    cok_images images = {0};
    cok_image_status status = cok_images_parse(data, size, 0, &images);
    bool ok = status == COK_IMAGE_OK;
    if (!ok) {
        fail(game, "%s%u.DAX record %u: %s", name, file, id, cok_image_status_string(status));
    } else {
        cok_picture_free(picture);
        cok_picture_status loaded = cok_picture_load(picture, images.images, images.count, -1);
        ok = loaded == COK_PICTURE_OK;
        if (!ok)
            fail(game, "%s%u.DAX record %u: %s", name, file, id,
                 cok_picture_status_string(loaded));
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
    cok_screen_adventure(&game->screen, &game->tiles, phase);
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

/* Show the 3D view again in place of a picture (6945:00ba), which is not
 * ported: the view is left blank. */
static void clear_view(cok_adventure *game)
{
    cok_picture_fill(&game->screen, 3, 3 * 8, 11, 11 * 8, 0);
}

/* PICTURE (2fd3:0914). */
static void picture(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t id = (uint8_t)cok_ecl_value(vm, 0);
    if (id == 0xff) {
        if (!(vm->last_mode == 4 && vm->mode != 4) && game->picture_shown) {
            clear_view(game);
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
        if (load_single(game, "BIGPIC", vm->file, id, &game->big))
            game->big_id = id;
        else
            log_text(game, "error", game->error);
    }
    uint16_t phase[3];
    moons(game, phase);
    cok_screen_big(&game->screen, &game->tiles, phase);
    if (game->big_id == id) cok_picture_draw(&game->screen, &game->big, 0, 1, 1, 0, NULL);
    /* Picture 0x79 runs 4877:0005 instead, which is not ported. */
    if (id != 0x79) game->big_shown = true;
}

/* CLEAR BOX (2fd3:3063). The party list (6346:07ba) and status line
 * (6346:2d75) are not ported. */
static void clear_box(cok_adventure *game)
{
    game->big_shown = false;
    cok_adventure_frame(game);
    draw_frame(game, 0);
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
    int choice = cok_menu_horizontal(&game->screen, &game->font, "", items, 13, 15,
                                     single ? 15 : 10, single, &game->selected, &keys, NULL);
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
                              .context = game};
    cok_ecl_init(&game->vm, &vm_hooks);
    game->vm.file = 1;
    game->picture_id = COK_ADVENTURE_NO_PICTURE;
    game->big_id = COK_ADVENTURE_NO_PICTURE;
    game->speed = 4;
    game->animate = true;
    game->selected = 1;
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
    /* The game loads its tile sets once, while the ECL file is still 1. */
    return load_single(game, "8X8D", 1, 202, &game->tiles);
}

void cok_adventure_close(cok_adventure *game)
{
    free_frames(game);
    cok_picture_free(&game->big);
    cok_picture_free(&game->tiles);
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

cok_ecl_status cok_adventure_enter(cok_adventure *game, uint8_t block)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_status status = cok_adventure_load(game, block);
    while (status == COK_ECL_OK && !vm->abort) {
        vm->reload = false;
        status = cok_ecl_run(vm, vm->vectors[4]);
        if (status != COK_ECL_OK || vm->abort || vm->reload) continue;
        status = cok_ecl_run(vm, vm->vectors[0]);
        if (status != COK_ECL_OK || vm->abort || vm->reload) continue;
        status = cok_ecl_run(vm, vm->vectors[1]);
        if (status != COK_ECL_OK || vm->abort || vm->reload) continue;
        break;
    }
    return status;
}
