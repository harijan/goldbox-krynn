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
        /* The original also redraws the party list (6346:07ba) and status
         * line (6346:2d75), which are not ported. */
        if (vm->mode != 3 && game->frame_pending) cok_adventure_frame(game);
        game->frame_pending = false;
    }
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
                              .context = game};
    cok_ecl_init(&game->vm, &vm_hooks);
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
    return true;
}

void cok_adventure_close(cok_adventure *game)
{
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
        status = cok_ecl_run(vm, vm->vectors[4]);
        if (status != COK_ECL_OK || vm->abort || vm->reload) continue;
        vm->mem4b00[0xf2] = vm->block;
        if (((vm->last_mode != 4 || vm->mode == 4) && game->files_loaded) ||
            (vm->last_mode == 4 && vm->mode == 4))
            cok_adventure_view(game);
        status = cok_ecl_run(vm, vm->vectors[0]);
        if (status != COK_ECL_OK || vm->abort || vm->reload) continue;
        status = cok_ecl_run(vm, vm->vectors[1]);
        /* The original then redraws the party list (6346:07ba), which is
         * not ported. */
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
    vm->mode = vm->mem4b00[0xe6] == 0 ? 3 : 4;
    cok_ecl_status status = cok_adventure_load(game, block);
    return status == COK_ECL_OK ? enter_block(game) : status;
}

/* The game clock. */

/* Units of the clock at 0x4bc6-0x4bcc and how many of each make the next
 * (DS:3874): 0x4bc9 is the hour. */
static const uint16_t clock_units[7] = {10, 10, 6, 24, 30, 12, 256};

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
        if (clock[i] < clock_units[i]) continue;
        /* A full last unit ages each character (field 0x60), and stays
         * full; characters are not ported. */
        if (i == 6) continue;
        ++clock[i + 1];
        clock[i] -= clock_units[i];
        if (i == 3) {
            moon(game, 0, 8);
            moon(game, 1, 1);
            moon(game, 2, 6);
        }
    }
}

void cok_adventure_pass_time(cok_adventure *game, unsigned unit, unsigned count)
{
    uint16_t clock[7];
    memcpy(clock, &game->vm.mem4b00[0xc6], sizeof clock);
    for (unsigned i = 0; i < count && unit < 7; ++i) {
        ++clock[unit];
        carry(game, clock);
    }
    memcpy(&game->vm.mem4b00[0xc6], clock, sizeof clock);
    /* 57e4:0171 then counts down the characters' spell effects, which are
     * not ported. */
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
    /* The original then redraws the status line (6346:2d75), which is not
     * ported. */
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
            /* 546c:3334 picks a character with the other keys, and the
             * party list (6346:07ba) is redrawn; neither is ported. */
            if (special) continue;
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
                break;
            case 'L':
                vm->mem7c00[0x2ca] |= 2;
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
        case 0x48: check_step(game); done = true; result = 0; break;
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

/* Offer the ways to open a locked door (475c:0e77). Returns true if it
 * opened. Characters are not ported, so the party is empty: as in the
 * original with no characters, Pick and Knock are not offered and Bash
 * fails. Choosing Pick, on either kind of door, stops it being offered
 * until the next step. */
static bool locked_door(cok_adventure *game)
{
    char items[41] = "";
    if (game->door_tries[0]) append(items, sizeof items, "Bash");
    /* " Pick" if 475c:0275 finds a thief, " Knock" if 475c:06c6 finds a
     * character with spell 0x1f. */
    append(items, sizeof items, " Exit");
    if (strcmp(items, " Exit") == 0) return false;
    bool special;
    int key = menu_read(game, "Locked. ", items, &special);
    if (key < 0 || special) return false;
    /* Bash (475c:02f3) rolls against each character's strength. */
    if (key == 'P') game->door_tries[1] = false;
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
        if (passage == 2 || passage == 3) moved = locked_door(game);
        if (moved) advance(game);
        /* The original then redraws the status line (6346:2d75). */
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

cok_ecl_status cok_adventure_play(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_status status = COK_ECL_OK;
    vm->keep_vars = false;
    game->moving = false;
    while (status == COK_ECL_OK && !vm->abort) {
        if (vm->mode != 4) {
            /* 475c:09ec and 475c:08d5 move the party on the overland map. */
            unported_command(game, "travel outside 3D areas");
            break;
        }
        int key = command(game);
        if (key < 0) break;
        if (!vm->reload) vm->mem4b00[0xf2] = vm->block;
        while (status == COK_ECL_OK && !vm->abort && (vm->mem7c00[0x2ca] > 1 || key == 'E')) {
            status = key == 'E' ? camp(game) : look(game);
            if (status == COK_ECL_OK && !vm->abort) key = command(game);
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
        cok_adventure_view(game);
        /* Sound 10 plays if the party moved. */
        game->picture_shown = false;
        game->view_replaced = true;
        status = cok_ecl_run(vm, vm->vectors[1]);
        if (status == COK_ECL_OK && !vm->abort && vm->reload) status = enter_block(game);
    }
    vm->abort = false;
    return status;
}
