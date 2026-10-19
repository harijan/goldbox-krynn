#include "adventure.h"

#include "arena.h"
#include "attack.h"
#include "camp.h"
#include "dax.h"
#include "cast.h"
#include "cloud.h"
#include "items.h"
#include "magic.h"
#include "monster.h"
#include "round.h"
#include "treasure.h"
#include "sheet.h"
#include "screen.h"
#include "start.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const cok_text_window cok_adventure_text_window = {1, 0x11, 0x26, 0x16};

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

/* Keys go through here so that the run stops when they run out. Crt's
 * ReadKey (1962:030f), as text input reads them. */
static int read_raw(void *context)
{
    cok_adventure *game = context;
    int key = game->keys.read == NULL ? -1 : game->keys.read(game->keys.context);
    if (key == COK_KEY_TIMEOUT && game->timed) return key;
    if (key == COK_KEY_DEVICE && game->device_wait != COK_WAIT_KEYS) return key;
    if (key < 0) {
        game->input_ended = true;
        game->vm.abort = true;
    }
    return key;
}

/* 1614:01ad, 00fa: with the mouse or the joystick on, a key, or what they
 * give (a click kept in game->pointer); a key typed meanwhile is read in
 * its place, the click kept (025b's KeyPressed after them). */
static int read_device(cok_adventure *game)
{
    game->device_wait = game->mouse.on ? COK_WAIT_MOUSE : COK_WAIT_STICK;
    int key;
    for (;;) {
        key = read_raw(game);
        if (key != COK_KEY_DEVICE) break;
        key = cok_adventure_device_key(game);
        if (key < 0) continue; /* up again already: wait on */
        game->device_wait = COK_WAIT_KEYS;
        if (game->hooks.key_pending != NULL && game->hooks.key_pending(game, game->hooks.context))
            key = read_raw(game);
        break;
    }
    game->device_wait = COK_WAIT_KEYS;
    return key;
}

/* 1614:025b, as every other read goes: no click yet (DS:8987); Ctrl-S,
 * or the scan code 0x13 after Alt-R's 0, turns the sound off or back on
 * (the key is returned all the same); then, in a game started with Woof,
 * the debug keys (cok_debug_key), which a scan code of 3 after Ctrl-2's 0
 * is too. */
static int read_key(void *context)
{
    cok_adventure *game = context;
    game->pointer.clicked = false;
    int key = cok_adventure_devices_on(game) ? read_device(game) : read_raw(game);
    if (key == 0x13) cok_adventure_sound(context, COK_SOUND_CTRL_S, 0x13);
    if (key >= 0) key = cok_debug_key(context, key);
    return key;
}

/* 1614:025b called directly: a key, but in the demonstration (DS:4b4b),
 * with neither the mouse nor the joystick on, 0 at once when none is
 * waiting. The menus wait for KeyPressed first, so they wait in the
 * demonstration too. */
static int wait_key(cok_adventure *game)
{
    if (game->demo && !cok_adventure_devices_on(game) && !cok_adventure_key_pending(game))
        return 0;
    return read_key(game);
}

int cok_adventure_wait_key(cok_adventure *game)
{
    return wait_key(game);
}

static void flush_keys(void *context)
{
    cok_adventure_flush(context);
}

static cok_keyboard keyboard(cok_adventure *game)
{
    return (cok_keyboard){read_key, game, flush_keys, &game->pointer};
}

/* Text input's keyboard (1521:0739 reads Crt.ReadKey itself). */
static cok_keyboard keyboard_raw(cok_adventure *game)
{
    return (cok_keyboard){read_raw, game, flush_keys, &game->pointer};
}

cok_keyboard cok_adventure_keyboard_raw(cok_adventure *game)
{
    return keyboard_raw(game);
}

static void menu_special(uint8_t scan, void *context);
static int menu_read(cok_adventure *game, const char *prompt, const char *items, bool *special);

/* Read a record by id from <name>.DAX (169c:088e); *no_file, if given, is
 * set when the archive cannot be opened. */
static uint8_t *find_record(cok_adventure *game, const char *name, uint8_t id, size_t *size,
                            bool *no_file)
{
    /* A game set up without its rules (cok_adventure_open names them). */
    assert(game->rules != NULL);
    char path[sizeof game->assets + 32];
    snprintf(path, sizeof path, "%s/%s.DAX", game->assets, name);
    /* 169c:088e: the sound stopped (command 0) before the file is read and
     * let on again (1) after, found or not. Reading takes no time here. */
    cok_adventure_sound(game, COK_SOUND_RECORD, 0);
    dax_archive archive = {0};
    dax_status status = dax_open(path, &archive);
    if (no_file != NULL) *no_file = status != DAX_OK;
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

static uint8_t *read_record(cok_adventure *game, const char *name, uint8_t id, size_t *size)
{
    return find_record(game, name, id, size, NULL);
}

/* The archive a descriptor's record is in: <stem><file>, or <stem> for
 * file 0. */
static void record_file(const cok_rules_record *r, char *out, size_t size)
{
    if (r->file == 0)
        snprintf(out, size, "%s", r->stem);
    else
        snprintf(out, size, "%s%u", r->stem, r->file);
}

uint8_t *cok_adventure_record(cok_adventure *game, const char *name, unsigned file,
                              uint8_t id, size_t *size)
{
    char label[32];
    snprintf(label, sizeof label, "%.20s%u", name, file);
    return read_record(game, label, id, size);
}

uint8_t *cok_adventure_find_record(cok_adventure *game, const char *name, uint8_t id,
                                   size_t *size, bool *no_file)
{
    return find_record(game, name, id, size, no_file);
}

/* Load frames as 127f:02a5 does in the game's display mode. */
static cok_picture_status load_frames(const cok_adventure *game, cok_picture *picture,
                                      const cok_image *frames, size_t count, int transparent)
{
    return game->display == COK_DISPLAY_CGA
               ? cok_picture_load_cga(picture, frames, count, transparent)
               : cok_picture_load(picture, frames, count, transparent);
}

cok_picture_status cok_adventure_create_picture(const cok_adventure *game, cok_picture *picture,
                                                size_t units, size_t height, size_t frames,
                                                int masked)
{
    cok_picture_status status = cok_picture_create(picture, units, height, frames, masked);
    if (status == COK_PICTURE_OK) picture->cga = game->display == COK_DISPLAY_CGA;
    return status;
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
    cok_image_status status = cok_images_parse_as(cok_game_rules(game), data, size, 0, &images);
    bool ok = status == COK_IMAGE_OK;
    if (!ok) {
        fail(game, "%s.DAX record %u: %s", name, id, cok_image_status_string(status));
    } else {
        cok_picture_status loaded =
            load_frames(game, picture, images.images, images.count, transparent);
        ok = loaded == COK_PICTURE_OK;
        if (!ok)
            fail(game, "%s.DAX record %u: %s", name, id, cok_picture_status_string(loaded));
    }
    cok_images_free(&images);
    free(data);
    if (ok) cok_adventure_flush(game); /* 127f:029a, once a record was loaded */
    return ok;
}

bool cok_adventure_load_image(cok_adventure *game, const char *name, uint8_t id, int transparent,
                              cok_picture *picture)
{
    return load_single(game, name, id, transparent, picture);
}

void cok_adventure_screen(cok_adventure *game, cok_screen screen)
{
    uint16_t phase[3];
    for (size_t i = 0; i < 3; ++i) phase[i] = cok_ecl_var_at(&game->vm, COK_VAR_MOONS, i);
    cok_screen_draw(&game->screen, &game->view.tiles[4], cok_game_rules(game), screen, phase);
}

void cok_adventure_frame(cok_adventure *game)
{
    cok_adventure_screen(game, COK_SCREEN_ADVENTURE);
}

/* Small pictures. */

static void free_frames(cok_adventure *game)
{
    for (size_t i = 0; i < game->frame_count; ++i) cok_picture_free(&game->frames[i]);
    game->frame_count = 0;
    game->frame = 0;
    game->frame_unset = true; /* 6961:0537 zeroes DS:6da2-6de3 */
    game->picture_id = COK_ADVENTURE_NO_PICTURE;
    game->picture_sprite = false;
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void prompt_colour(cok_adventure *game, const char *text, uint8_t fg);

void cok_adventure_loading(cok_adventure *game)
{
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    cok_text_string(&game->screen, &game->font, "Loading...Please Wait", 0, 24, 10, 0);
}

/* The file a record is read from by its id, where the game chooses it so
 * (files, by id: dkk 6ca1:00a4, 6d21:0ec5), or the ECL file. (For a wall
 * set's id its table does not name, dkk 6d21:0ec5 builds the name from a
 * string it never set; the port takes the ECL file's.) */
static unsigned file_by_id(const cok_adventure *game, const uint8_t *files, uint8_t id)
{
    return files != NULL && files[id] != 0 ? files[id] : game->vm.file;
}

/* Load record id of <name><file> into the small picture's slot unless it
 * holds it (6961:00e4), which goes by the name and the record (DS:6dee,
 * 6e00), not the file, and keeps both before it reads: a record that is
 * not there says "PIC not found" (1521:096c: row 24 in yellow, a key),
 * leaves the slot empty and counts as loaded. With animation on (DS:4b4f)
 * "Loading...Please Wait" shows on row 24 while it reads and the row is
 * cleared after (not when the record is not there). Returns the record,
 * or NULL. */
static uint8_t *picture_record(cok_adventure *game, const char *name, uint8_t id, bool sprite,
                               size_t *size)
{
    if (id == 0xff || (id == game->picture_id && sprite == game->picture_sprite)) return NULL;
    if (game->animate) cok_adventure_loading(game); /* 6961:0128 */
    free_frames(game);                              /* 6961:0537 */
    game->picture_id = id;
    game->picture_sprite = sprite;
    game->error[0] = '\0';
    const cok_rules_archives *a = &cok_game_rules(game)->archives;
    unsigned file = file_by_id(game, sprite ? a->sprite_files : a->pic_files, id);
    char label[32];
    snprintf(label, sizeof label, "%s%u", name, file);
    bool no_file;
    uint8_t *data = find_record(game, label, id, size, &no_file);
    if (data != NULL) {
        game->frame_unset = false; /* DS:6da3 1 */
        return data;
    }
    if (no_file) {
        /* A file that is not there: the original asks for it ("Couldn't
         * find PIC1.dax. Check install.", 169c:02a7 with DS:5884 0) and
         * looks again after each key until it is there; the port stops,
         * as for portraits. */
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return NULL;
    }
    static const char missing[] = "PIC not found";
    log_text(game, "print", missing);
    prompt_colour(game, missing, 14);
    return NULL;
}

/* The end of 6961:00e4: the keys flushed (6961:0514), and with animation
 * on row 24 cleared. */
static void picture_loaded(cok_adventure *game)
{
    cok_adventure_flush(game); /* 6961:0514 */
    if (game->animate) cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 6961:052c */
}

/* Load PIC<file> record id (6961:00e4 with 0). Groups after the first are
 * XOR deltas; without animation only the first is kept. */
static void load_picture(cok_adventure *game, uint8_t id)
{
    size_t size;
    const char *name = cok_game_rules(game)->archives.pic;
    uint8_t *data = picture_record(game, name, id, false, &size);
    if (data == NULL) return;
    cok_images images = {0};
    cok_image_status status = cok_images_undelta(data, size);
    if (status == COK_IMAGE_OK)
        status = cok_images_parse_as(cok_game_rules(game), data, size, 1, &images);
    if (status != COK_IMAGE_OK) {
        fail(game, "%s%u.DAX record %u: %s", name, game->vm.file, id,
             cok_image_status_string(status));
        log_text(game, "error", game->error);
        free(data);
        return;
    }
    size_t count = game->animate ? images.count : 1;
    if (count > COK_ADVENTURE_FRAMES) count = COK_ADVENTURE_FRAMES;
    for (size_t i = 0; i < count; ++i) {
        /* Each group's delay comes just before its header. */
        game->delays[i] = u32(images.images[i].pixels - 17 - 4);
        if (load_frames(game, &game->frames[i], &images.images[i], 1, -1) != COK_PICTURE_OK)
            break;
        game->frame_count = i + 1;
    }
    cok_images_free(&images);
    free(data);
    picture_loaded(game);
}

void cok_adventure_load_sprite(cok_adventure *game, uint8_t id)
{
    size_t size;
    const char *name = cok_game_rules(game)->archives.sprite;
    uint8_t *data = picture_record(game, name, id, true, &size);
    if (data == NULL) return;
    cok_images images = {0};
    cok_image_status status = cok_images_parse_as(cok_game_rules(game), data, size, 1, &images);
    if (status != COK_IMAGE_OK) {
        fail(game, "%s%u.DAX record %u: %s", name, game->vm.file, id,
             cok_image_status_string(status));
        log_text(game, "error", game->error);
        free(data);
        return;
    }
    size_t count = images.count < COK_ADVENTURE_FRAMES ? images.count : COK_ADVENTURE_FRAMES;
    for (size_t i = 0; i < count; ++i) {
        game->delays[i] = u32(images.images[i].pixels - 17 - 4);
        /* Outside CGA mode colour 13 is drawn black (6961:0497). */
        cok_picture_status loaded =
            game->display == COK_DISPLAY_CGA
                ? cok_picture_load_cga(&game->frames[i], &images.images[i], 1, 0)
                : cok_picture_load_sprite(&game->frames[i], &images.images[i], 1);
        if (loaded != COK_PICTURE_OK) break;
        game->frame_count = i + 1;
    }
    cok_images_free(&images);
    free(data);
    picture_loaded(game);
}

void cok_adventure_draw_sprite(cok_adventure *game, unsigned frame)
{
    if (frame < 1 || frame > game->frame_count) return;
    const cok_picture *p = &game->frames[frame - 1];
    cok_picture_draw(&game->screen, p, 0, p->x + 3, p->y + 3, COK_DRAW_MASKED, NULL);
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
    cok_adventure_screen(game, COK_SCREEN_BIG);
    if (game->big.pixels != NULL) cok_picture_draw(&game->screen, &game->big, 0, 1, 1, 0, NULL);
}

/* Draw the 3D view from the party's square (69ea:0820), under the sky that
 * 6945:00ba last picked (DS:6d80), or the overhead map while DS:6d84 is set
 * (69ea:000f). The original draws the map's arrow outside the view for a
 * square off the map; the run stops. */
static void draw_view(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (game->overhead) {
        if (!cok_view_overhead(&game->screen, &game->view, vm->map_x, vm->map_y,
                               vm->direction))
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "the overhead map of square %d,%d facing %u (69ea:000f)",
                               vm->map_x, vm->map_y, vm->direction);
        return;
    }
    cok_view_backdrop backdrop = {
        .sky = game->sky,
        .line = game->backdrop[0],
        .horizon = game->backdrop[1],
        .ground = game->backdrop[2],
        .hour = cok_ecl_var_at(vm, COK_VAR_CLOCK, COK_CLOCK_HOURS),
    };
    cok_view_draw(&game->screen, &game->view, vm->map_x, vm->map_y, vm->direction, &backdrop);
}

/* Turn the overhead map off (DS:6d84). */
static void overhead_off(cok_adventure *game)
{
    if (game->overhead) log_text(game, "area", "off");
    game->overhead = false;
}

/* Whether the view is left alone after camp and in a mode-4 redraw:
 * Champions' 0x4c38 set (2fd3:3403, 6346:2c17); a game without the
 * variable always draws it (dkk 3632:237e, 65cd:2db0). */
static bool no_view(const cok_ecl *vm)
{
    return cok_ecl_has_var(vm->rules, COK_VAR_NO_VIEW) && cok_ecl_flag(vm, COK_VAR_NO_VIEW);
}

void cok_adventure_view(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (game->picture_id == 9) game->redraw = false;
    if (cok_ecl_var(vm, COK_VAR_3D) == 0 && !cok_ecl_flag(vm, COK_VAR_OFF_MAP)) {
        if (game->redraw) draw_big(game);
    } else {
        vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
        game->sky = cok_view_sky_color(
            cok_ecl_var(vm, vm->square < 0x80 ? COK_VAR_SKY_LOW : COK_VAR_SKY_HIGH));
        if (game->display != COK_DISPLAY_CGA) {
            game->backdrop[2] = 8;
        } else if (vm->c059_changed) {
            /* DS:884f: from 0xc05f (DS:72d1) and 0xc059 (DS:72d0), by
             * 6945:0014, 0048 and 007c. */
            game->backdrop[0] = vm->c05f == 8 || vm->c05f == 0x41 ? 0 : 15;
            game->backdrop[1] = vm->c05f == 8 ? 8 : 7;
            game->backdrop[2] = vm->c059 == 0x0b ? 8 : vm->c059 == 6 ? 1 : 6;
        }
        /* Where the square is hidden, the overhead map goes off. */
        if (cok_ecl_var(vm, COK_VAR_HIDDEN) != 0) overhead_off(game);
        draw_view(game);
    }
    game->redraw = false;
}

void cok_adventure_overhead_off(cok_adventure *game)
{
    if (!game->overhead) return;
    overhead_off(game);
    game->redraw = true;
    cok_adventure_view(game);
}

void cok_adventure_load_big(cok_adventure *game, uint8_t id)
{
    free_frames(game);
    if (game->big_id == id && game->big.pixels != NULL) return;
    char name[16];
    snprintf(name, sizeof name, "%s%u", cok_game_rules(game)->archives.big, game->vm.file);
    /* 127f:0111 frees the old picture and, for a record that is not there,
     * returns with none; DS:6e06 takes the id all the same. A missing
     * file, where the original asks for the disk, is logged. */
    game->big_id = id;
    if (load_single(game, name, id, -1, &game->big)) return;
    bool no_file;
    size_t size;
    free(find_record(game, name, id, &size, &no_file));
    if (no_file) log_text(game, "error", game->error);
}

/* Show text on row 24 in fg and wait for a key (1521:096c). */
static void prompt_colour(cok_adventure *game, const char *text, uint8_t fg)
{
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    cok_text_string(&game->screen, &game->font, text, 0, 24, fg, 0);
    wait_key(game);
}

void cok_adventure_prompt_colour(cok_adventure *game, const char *text, uint8_t fg)
{
    prompt_colour(game, text, fg);
}

/* The portrait. */

/* Load record id of HEAD<file> or BODY<file> into piece, opaque
 * (127f:0111 with 0, 0), unless it holds that record (*loaded; DS:6de4,
 * 6de9) or id is 0xff (6961:05b9). The file's number is cut to one digit,
 * as Str with a width of 1 makes it. A record that is not there leaves
 * no picture and says "head not found", for the body too; a file that is
 * not there fails. */
static bool load_piece(cok_adventure *game, const char *kind, uint8_t id, uint8_t *loaded,
                       cok_picture *piece)
{
    if (id == 0xff || (*loaded != 0xff && *loaded == id)) return true;
    char name[16];
    snprintf(name, sizeof name, "%s%c", kind,
             cok_game_rules(game)->archives.file_digit(game->vm.file));
    game->error[0] = '\0';
    bool no_file;
    size_t size;
    uint8_t *data = find_record(game, name, id, &size, &no_file);
    free(data);
    if (no_file) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    if (data == NULL) {
        cok_picture_free(piece);
        static const char missing[] = "head not found";
        log_text(game, "print", missing);
        prompt_colour(game, missing, 14);
    } else if (!load_single(game, name, id, -1, piece)) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    *loaded = id;
    return true;
}

/* Draw a piece of the portrait at cell x, y, opaque and unclipped
 * (6961:000a with 0, 127f:10e7): nothing for none. */
static bool draw_piece(cok_adventure *game, const cok_picture *piece, int x, int y)
{
    if (piece->pixels == NULL) return true;
    if (x + piece->units > 40 || y * 8 + piece->height > 200) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a portrait's piece of %u units by %u rows at cell %d, %d runs off "
                           "the screen, which 127f:10e7 does not clip", piece->units,
                           piece->height, x, y);
        return false;
    }
    cok_picture_draw(&game->screen, piece, 0, x, y, 0, NULL);
    return true;
}

/* Load the portrait (6961:05b9) and draw the head at cell 3, 3 and the
 * body five cells below it (6961:06bd with 1). */
static void show_portrait(cok_adventure *game, uint8_t head, uint8_t body)
{
    const cok_rules_archives *a = &cok_game_rules(game)->archives;
    if (!load_piece(game, a->head, head, &game->head_id, &game->head) ||
        !load_piece(game, a->body, body, &game->body_id, &game->body) || game->vm.abort)
        return;
    cok_adventure_flush(game); /* 1614:045c */
    if (draw_piece(game, &game->head, 3, 3)) draw_piece(game, &game->body, 3, 8);
}

void cok_adventure_portrait(cok_adventure *game, uint8_t head, uint8_t body)
{
    game->view_replaced = false; /* DS:884b */
    game->portrait_head = head;
    game->portrait_body = body;
    show_portrait(game, head, body);
}

void cok_adventure_forget_portrait(cok_adventure *game)
{
    cok_picture_free(&game->head);
    cok_picture_free(&game->body);
    game->head_id = game->body_id = 0xff;
}

/* PICTURE (2fd3:0914). */
static void picture(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t id = (uint8_t)cok_ecl_value(vm, 0);
    if (id == 0xff) {
        /* An encounter's sprite over the view (DS:884d) is erased too. */
        if (!(vm->last_mode == 4 && vm->mode != 4) &&
            (game->picture_shown || game->sprite_shown)) {
            game->redraw = true;
            cok_adventure_view(game);
            game->picture_shown = false;
            game->sprite_shown = false;
            game->view_replaced = true;
        }
        game->sprite_loaded = game->closeup_shown = false; /* DS:8830, 8831 */
        return;
    }
    game->closeup_shown = true;
    game->picture_shown = true;
    if (cok_ecl_has_var(vm->rules, COK_VAR_PORTRAIT) && cok_ecl_var(vm, COK_VAR_PORTRAIT) != 0xff) {
        /* With 0x7ee1 set, a portrait: 0x7ee1's head over this body (a
         * game without portraits has no such variable: dkk 3632:096c). */
        cok_adventure_portrait(game, (uint8_t)cok_ecl_var(vm, COK_VAR_PORTRAIT), id);
        return;
    }
    game->view_replaced = true;
    if (id < 0x70) {
        /* Loading starts the frames from the first (DS:6da3); a picture
         * already loaded keeps its current one. The first is drawn. */
        load_picture(game, id);
        draw_frame(game, 0);
        return;
    }
    cok_adventure_load_big(game, id);
    draw_big(game);
    /* The overland map, 0x79 in Champions, marks the party on it instead
     * (4877:0005). */
    if (id == cok_game_rules(game)->view.overland_picture)
        cok_adventure_mark(game);
    else
        game->big_shown = true;
    game->redraw = false;
}

/* The 3D view's files. */

/* Load tile set set from 8X8D<file> record id, the set's colour of the
 * game's transparent (6e22:0050: 13; dkk 70eb:002f 0 for set 4), first
 * writing "slotnum = SET" to DEBUG.TXT while the debug
 * flag is set. Tile set 0's file is chosen by the record's id where the
 * game chooses the wall sets' so (dkk 70eb:002f); the frame's is 4 and
 * Champions' wall sets load sets 1-3. */
static bool load_tiles(cok_adventure *game, unsigned set, uint8_t id)
{
    if (game->debug) {
        char line[32];
        snprintf(line, sizeof line, "slotnum = %u", set);
        cok_debug_write(game, line);
    }
    const cok_rules_archives *a = &cok_game_rules(game)->archives;
    unsigned file = set == 0 ? file_by_id(game, a->wall_files, id) : game->vm.file;
    char name[16];
    snprintf(name, sizeof name, "%s%u", a->tiles, file);
    return load_single(game, name, id, cok_game_rules(game)->view.tile_transparent[set],
                       &game->view.tiles[set]);
}

/* Load GEO<file> record id as the map (69ea:130d). */
static bool load_map(cok_adventure *game, uint8_t id)
{
    cok_ecl *vm = &game->vm;
    game->error[0] = '\0';
    size_t size;
    const char *name = cok_game_rules(game)->archives.geo;
    uint8_t *data = cok_adventure_record(game, name, vm->file, id, &size);
    if (data == NULL) return false;
    bool ok = cok_view_set_map(&game->view, data, size);
    free(data);
    if (!ok) {
        fail(game, "%s%u.DAX record %u is not a map", name, vm->file, id);
        return false;
    }
    cok_ecl_set_var(vm, COK_VAR_MAP, id);
    return true;
}

/* Load WALLDEF<file> record id into the wall sets from slot on, then the
 * tile sets they use (69ea:1025): one each, or one for the record where
 * the game's sets share one (dkk 6d21:0ec5, with the backdrop the record's
 * id names). The file is the ECL file's, or the wall set's by its id (dkk
 * 6d21:0ec5). Slots other than the game's (1-3 in Champions, 0-3) are
 * ignored. */
static bool load_walls(cok_adventure *game, unsigned slot, uint8_t id)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_rules_view *shape = &rules->view;
    if (slot < shape->first_slot || slot > 3) return true;
    game->error[0] = '\0';
    size_t size;
    const char *name = rules->archives.walldef;
    unsigned file = file_by_id(game, rules->archives.wall_files, id);
    uint8_t *data = cok_adventure_record(game, name, file, id, &size);
    if (data == NULL) return false;
    size_t sets = cok_view_set_walls(&game->view, slot, data, size);
    free(data);
    if (sets == 0) {
        fail(game, "%s%u.DAX record %u does not fit from wall set %u", name, file, id, slot);
        return false;
    }
    unsigned at = slot - shape->first_slot;
    for (size_t i = 0; i < sets; ++i) {
        game->wall_ids[at + i] = game->wall_slots[at + i] = -1;
        if (shape->shared_tiles) continue;
        uint8_t tiles = sets < 2 ? id : (uint8_t)(id * 10 + i + 1);
        if (!load_tiles(game, slot + (unsigned)i, tiles)) return false;
    }
    if (shape->shared_tiles && !load_tiles(game, slot, id)) return false;
    if (rules->archives.backdrop != NULL) {
        /* BACK<file> record id, without a mask (DS:42d0's 0). */
        char back[16];
        snprintf(back, sizeof back, "%s%u", rules->archives.backdrop, file);
        if (!load_single(game, back, id, -1, &game->view.back)) return false;
    }
    game->wall_ids[at] = id;
    game->wall_slots[at] = (int16_t)slot;
    return true;
}

/* Mark wall set slot empty in DS:6d8a. */
static void no_walls(cok_adventure *game, unsigned slot)
{
    unsigned at = slot - cok_game_rules(game)->view.first_slot;
    game->wall_ids[at] = game->wall_slots[at] = -1;
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
    if (cok_ecl_meaning(vm) == COK_ECL_LOAD_FILES) {
        game->map_loaded = true;
        if (id[1] != 0xff && id[1] != 0x7f && cok_ecl_var(vm, COK_VAR_3D) != 0) {
            ok = load_map(game, id[1]);
            cok_ecl_set_var(vm, COK_VAR_STAY, 0);
        }
    } else {
        game->pieces_loaded = true;
        if (id[1] == 0x7f) {
            ok = load_walls(game, 1, 0);
        } else if (cok_ecl_var_at(vm, COK_VAR_PIECES, 0) == 0 ||
                   cok_ecl_var_at(vm, COK_VAR_PIECES, 1) == 0) {
            for (unsigned slot = 1; slot <= 3 && ok; ++slot) {
                if (id[slot] == 0xff)
                    no_walls(game, slot);
                else
                    ok = load_walls(game, slot, id[slot]);
            }
        } else {
            /* Slot 1 in Champions, 0 in Death Knights (dkk 3632:0daa). */
            unsigned first = cok_game_rules(game)->view.first_slot;
            if (id[1] == 0xff)
                no_walls(game, first);
            else
                ok = load_walls(game, first, id[1]);
            if (id[3] == 0xff || id[3] == 0x7f)
                no_walls(game, 3);
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
    /* Not in the demonstration (2fd3:3086). */
    if (!game->demo) draw_frame(game, 0);
    cok_adventure_status(game);
    game->frame_pending = false;
}

/* Text. */


static void page(void *context)
{
    cok_adventure *game = context;
    wait_key(game);
    cok_adventure_flush(game); /* 1614:045c */
}

/* PRINT and PRINTCLEAR (2fd3:0acf): typed (DS:4b59 set at 0ae3), which
 * leaves the text delay off (0b62). */
static void print(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (vm->operand[0].type < 0x80)
        snprintf(vm->string[0], sizeof vm->string[0], "%u", cok_ecl_value(vm, 0));
    bool clear = cok_ecl_meaning(vm) == COK_ECL_PRINTCLEAR;
    if (clear) vm->cursor = (cok_text_cursor){1, 0x11};
    log_text(game, "print", vm->string[0]);
    game->text_shown = true;
    game->text_delay = true;
    cok_adventure_wrap(game, &vm->cursor, vm->string[0], cok_adventure_text_window, 10, clear,
                       page);
    game->text_delay = false;
}

/* Append as much of text as fits, as a Pascal string assignment cuts it. */
static void append(char *dst, size_t size, const char *text)
{
    size_t length = strlen(dst);
    while (*text != '\0' && length + 1 < size) dst[length++] = *text++;
    dst[length] = '\0';
}

/* HORIZONTAL MENU (2fd3:116d; dkk 3632:11fd without its single item's case,
 * rules->ecl.menu_single_item). */
static void horizontal_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    size_t count = vm->operands;
    game->selected = 1;
    bool single_item = cok_game_rules(game)->ecl.menu_single_item;
    bool single = count == 1 && single_item;
    /* 2fd3:1122 replaced by 2fd3:1146, which has no full stop. */
    if (single && strcmp(vm->string[0], "PRESS BUTTON OR RETURN TO CONTINUE.") == 0)
        snprintf(vm->string[0], sizeof vm->string[0], "PRESS <ENTER>/<RETURN> TO CONTINUE");
    /* Items are ~ITEM separated by spaces, cut to 50 characters as each is
     * added. */
    char items[51] = "";
    for (size_t i = 0; i < count && i < COK_ECL_MAX_STRINGS; ++i) {
        append(items, sizeof items, "~");
        append(items, sizeof items, vm->string[i]);
        if (i + 1 < count) append(items, sizeof items, " ");
    }
    game->menu_animates = game->picture_shown && (game->view_replaced || !single_item);
    if (game->menu_animates) draw_frame(game, game->frame);
    int choice = cok_adventure_horizontal(game, "", items, single ? 15 : 10, single);
    game->menu_animates = false;
    if (choice < 0) return;
    char text[16];
    snprintf(text, sizeof text, "%d", choice);
    log_text(game, "choice", text);
    cok_ecl_store_as(vm, &vm->header[0], (uint16_t)choice);
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
}

int cok_adventure_horizontal(cok_adventure *game, const char *prompt, const char *items,
                             uint8_t normal, bool enter_returns)
{
    log_text(game, "menu", items);
    cok_keyboard keys = keyboard(game);
    cok_menu_hooks hooks = {menu_special, game};
    return cok_menu_horizontal(&game->screen, &game->font, prompt, items, 13, 15, normal,
                               enter_returns, &game->selected, &keys, &hooks);
}

/* VERTICAL MENU (2fd3:0f9d): print the prompt, then list the items below
 * it. The index is stored even when the list is cancelled. */
static void vertical_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
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
    cok_adventure_wrap(game, &vm->cursor, vm->header_string, cok_adventure_text_window, 10, true,
                       page);
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
    cok_ecl_store_as(vm, &vm->header[0], (uint16_t)index);
    cok_picture_fill(&game->screen, 1, 0x11 * 8, 0x26, 6 * 8, 0);
}

/* INPUT NUMBER (2fd3:0a11) and INPUT STRING (2fd3:0a57). */
static void input(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t address = cok_ecl_address(vm, 1);
    cok_keyboard keys = keyboard_raw(game);
    if (cok_ecl_meaning(vm) == COK_ECL_INPUT_NUMBER) {
        uint16_t number;
        if (!cok_text_number(&game->screen, &game->font, "", 10, 0, &keys, &number))
            return;
        char text[8];
        snprintf(text, sizeof text, "%u", number);
        log_text(game, "input", text);
        cok_ecl_store_to(vm, 1, number);
        return;
    }
    char line[41];
    if (!cok_text_input(&game->screen, &game->font, "", 10, 0, 40, &keys, line)) return;
    if (line[0] == '\0') snprintf(line, sizeof line, " ");
    log_text(game, "input", line);
    cok_ecl_store_string(vm, address, line);
}

/* The party. */

cok_character *cok_adventure_selected(cok_adventure *game)
{
    uint8_t *record = game->vm.character;
    if (record == NULL) return NULL;
    if (game->outside != NULL && game->outside->record == record) return game->outside;
    size_t i = cok_party_index(&game->party, record);
    return i < game->party.count ? game->party.members[i] : NULL;
}

void cok_adventure_party(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if ((vm->mode == 3 && !cok_ecl_flag(vm, COK_VAR_OFF_MAP)) || game->big_shown) return;
    /* 6346:07f1-0866: where a click picks from it, a row a member. */
    game->party_left = vm->mode == 0 ? 1 : 0x11;
    game->party_right = 0x26;
    game->party_top = 4;
    game->party_bottom = (uint8_t)(3 + game->party.count);
    cok_party_draw(&game->screen, &game->font, &game->party, cok_game_rules(game), vm->character,
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
    if (vm->mode == 3) return;
    char hours[3], minutes[3], text[64] = "";
    two_digits(hours, cok_ecl_var_at(vm, COK_VAR_CLOCK, COK_CLOCK_HOURS));
    two_digits(minutes, (uint16_t)(10 * cok_ecl_var_at(vm, COK_VAR_CLOCK, COK_CLOCK_TENS) +
                                   cok_ecl_var_at(vm, COK_VAR_CLOCK, COK_CLOCK_MINUTES)));
    if (cok_ecl_var(vm, COK_VAR_HIDDEN) == 0)
        snprintf(text, sizeof text, "%u,%u ", (uint8_t)vm->map_x, (uint8_t)vm->map_y);
    /* 0x4cff turns the names, by eighths outside 3D areas. A sum of 16 or
     * more reads past the original's table; it wraps here. A game without
     * it turns none (Death Knights' 65cd:2eea). */
    uint16_t turn =
        cok_ecl_has_var(vm->rules, COK_VAR_TURN) ? cok_ecl_var(vm, COK_VAR_TURN) : 0;
    unsigned dir = (uint8_t)(vm->direction + (cok_ecl_var(vm, COK_VAR_3D) != 0 ? 2 : 1) * turn);
    if (dir >= 8) dir -= 8;
    append(text, sizeof text, directions[dir % 8]);
    append(text, sizeof text, " ");
    append(text, sizeof text, hours);
    append(text, sizeof text, ":");
    append(text, sizeof text, minutes);
    if (game->debug) append(text, sizeof text, "*"); /* 6346:2f4c, DS:4b51 */
    if (vm->mode == 2)
        append(text, sizeof text, " camping");
    else if ((cok_ecl_var(vm, COK_VAR_SEARCH) & 1) != 0)
        append(text, sizeof text, " search");
    text[40] = '\0';
    cok_picture_fill(&game->screen, 0x11, 15 * 8, 0x26 - 0x11 + 1, 8, 0); /* 1128:07e6 */
    cok_text_string(&game->screen, &game->font, text, 0x11, 15, 10, 0);
}

static uint16_t character_value(cok_ecl *vm, uint16_t address, void *context)
{
    cok_adventure *game = context;
    if (address == 0x7cc9)
        return vm->character != NULL &&
               cok_character_former_class(vm->character, cok_game_rules(game));
    return (uint8_t)cok_party_index(&game->party, vm->character); /* 0x7eb1, 0x7eb4 */
}

/* A special key from a menu picks a character (546c:3334). */
static void pick_member(cok_adventure *game, uint8_t scan)
{
    game->vm.character = cok_adventure_special(game, scan);
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
    prompt_colour(game, text, 15);
}

void cok_adventure_prompt_key(cok_adventure *game, const char *text)
{
    prompt_key(game, text);
}

void cok_adventure_alert(cok_adventure *game, const char *text, uint8_t fg)
{
    log_text(game, "print", text);
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    cok_text_string(&game->screen, &game->font, text, 0, 24, fg, 0);
    wait_key(game);
}

/* Print text in the text window in colour fg (1521:04ac). */
static void print_text(cok_adventure *game, const char *text, uint8_t fg, bool clear)
{
    log_text(game, "print", text);
    game->text_shown = true;
    cok_adventure_wrap(game, &game->vm.cursor, text, cok_adventure_text_window, fg, clear, page);
}

/* A character's name, from its Pascal string. */
static void name_of(const cok_character_layout *L, const uint8_t *c, char out[16])
{
    uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(out, c + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
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
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t classes = 0;
    for (size_t i = 0; i < 8; ++i)
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, i) > 0) ++classes;
    /* The original divides before it tests whether the character can act. */
    if (classes == 0) {
        game->vm.status = COK_ECL_DIVIDE_BY_ZERO;
        return;
    }
    if (cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0) return;
    uint32_t total = cok_field_u32(L, c, COK_FIELD_EXPERIENCE);
    total += (uint16_t)(points / classes);
    cok_field_set32(L, c, COK_FIELD_EXPERIENCE, total);
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
        if (vm->character != NULL) name_of(cok_game_rules(game)->character, vm->character, name);
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
 * the special keys whose scan codes are 'E' and 'S': 0x53 Del, and 0x45
 * NumLock, which the BIOS never queues for Crt.ReadKey. A click on the
 * party list (in the start menu too, DS:8985 set for the menu) picks its
 * member: into the pick (DS:6e08 set, DS:6e09) with into, else into the
 * selection itself (DS:6096), as WHO's. Returns the character, or NULL;
 * *ended is set if input ended. */
static uint8_t *pick_character(cok_adventure *game, const char *prompt, uint8_t *who,
                               bool exit_item, bool into, bool *ended)
{
    cok_ecl *vm = &game->vm;
    bool party_clicks = game->mouse.party_clicks;
    game->pick_into = into;
    game->pick = vm->character;
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
        /* In camp (and mode 6) 67b5:03e2 shows the small picture. */
        game->menu_animates = vm->mode == 2 || vm->mode == 6;
        if (game->menu_animates) draw_frame(game, game->frame);
        bool special;
        if (vm->mode == 0) game->mouse.party_clicks = true; /* 6346:335c */
        key = menu_read(game, menu_prompt, items, &special);
        game->mouse.party_clicks = party_clicks;
        game->menu_animates = false;
        if (key < 0) {
            game->pick_into = false;
            *ended = true;
            return who;
        }
        if (!special) {
            if (exit_item && (key == 'E' || key == 0)) who = NULL;
        } else if (key == 0x48 && game->pointer.clicked) {
            /* 6346:340a: the member the click picked. */
            who = into ? game->pick : vm->character;
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
    game->pick_into = false; /* 6346:34de */
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
    uint8_t *picked = pick_character(game, vm->string[0], vm->character, false, false, &ended);
    vm->character = picked;
    game->moving = moving;
    if (ended) return;
    char name[16] = "";
    if (picked != NULL) name_of(cok_game_rules(game)->character, picked, name);
    log_text(game, "who", name);
}

/* The dice (60f4:1216). */
static uint8_t roll(cok_adventure *game, uint8_t count, uint8_t sides)
{
    return cok_dice(&game->vm.seed, count, sides);
}

/* The party member whose record is c, or NULL. */
static cok_character *member_of(cok_adventure *game, const uint8_t *c)
{
    size_t i = cok_party_index(&game->party, c);
    return i < game->party.count ? game->party.members[i] : NULL;
}

bool cok_adventure_effect_failed(cok_adventure *game)
{
    if (game->vm.status == COK_ECL_OK)
        cok_adventure_fail(game, COK_ECL_EFFECT_FAILED, "%s", game->effects.error);
    return false;
}

static void effect_failed(cok_adventure *game)
{
    cok_adventure_effect_failed(game);
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
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (cok_field_u8(L, c, COK_FIELD_STATUS) == 6) return;
    char name[16], text[80];
    name_of(L, c, name);
    if (cok_field_u8(L, c, COK_FIELD_HIT_POINTS) + 10 < damage) {
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
    cok_character_damage(c, cok_game_rules(game), (uint8_t)damage);
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
 * one the port cannot carry out stops the run with COK_ECL_EFFECT_FAILED. */
static void damage(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t *saved = vm->character;
    uint8_t op[5];
    for (size_t i = 0; i < 5; ++i) op[i] = (uint8_t)cok_ecl_value(vm, i);
    uint8_t size = (uint8_t)cok_ecl_var(vm, COK_VAR_PARTY_SIZE);
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
        if (cok_field_u8(cok_game_rules(game)->character, game->party.members[i]->record,
                         COK_FIELD_ACTIVE) != 0)
            alive = true;
    game->party_killed = !alive;
    /* The original sets DS:4b57, which ends the run, to whether the party
     * died; here it also stays set when input has ended. */
    vm->abort = !alive || game->input_ended;
    if (!alive) {
        cok_adventure_screen(game, COK_SCREEN_OPEN); /* 1128:0000 */
        vm->cursor = (cok_text_cursor){2, 2};
        const char *text = "The entire party is killed!";
        log_text(game, "print", text);
        cok_adventure_wrap(game, &vm->cursor, text, (cok_text_window){1, 1, 0x26, 0x16}, 10, true,
                           page);
        wait_ms(game, 3000);
    }
    vm->character = saved;
    prompt_key(game, "press <enter>/<return> to continue");
}

/* Log the party's square and facing ("at"). */
static void log_position(cok_adventure *game)
{
    char text[32];
    snprintf(text, sizeof text, "%d,%d,%u", game->vm.map_x, game->vm.map_y, game->vm.direction);
    log_text(game, "at", text);
}

/* Whether 69ea:06a2 gives 0 for the party's square before it looks at the
 * facing: off the map, where the block has no squares (DS:8846 0 or
 * 0x50). */
static bool no_square(const cok_adventure *game)
{
    const cok_ecl *vm = &game->vm;
    bool on_map = vm->map_x >= 0 && vm->map_x <= 15 && vm->map_y >= 0 && vm->map_y <= 15;
    return !on_map && !game->view.wrap;
}

static bool facing_known(unsigned dir)
{
    return dir <= 6 && dir % 2 == 0;
}

/* The wall ahead (69ea:06a2) after CALL's step forward (3775:1bed). For a
 * facing other than 0, 2, 4 and 6, which only the overland map or a saved
 * game gives, 06a2 returns its local [bp-1] unset: there it holds the byte
 * 3775:1bed's call to 69ea:07a5 pushed, the high byte of AX, which still
 * holds CALL's address less 0x7fff, 0x401f, so 0x40. */
static uint8_t wall_after_step(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (no_square(game) || facing_known(vm->direction))
        return cok_view_wall(&game->view, vm->direction, vm->map_x, vm->map_y);
    return 0x40;
}

/* A step forward (3775:1bed): the party's square moves one the way it
 * faces, wrapping within 0-15 (a coordinate already past 15 going on to
 * 0, one below 0 going back to 15, as signed bytes), not at all for
 * another facing; then the square byte and the wall ahead are worked out
 * again and the view is marked as changed (DS:8850), so that the next
 * CALL [2e10] draws it. Logged as "at", as the loop's steps are. */
static void step_forward(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    switch (vm->direction) {
    case 0: vm->map_y = (int8_t)(vm->map_y > 0 ? vm->map_y - 1 : 15); break;
    case 2: vm->map_x = (int8_t)(vm->map_x < 15 ? vm->map_x + 1 : 0); break;
    case 4: vm->map_y = (int8_t)(vm->map_y < 15 ? vm->map_y + 1 : 0); break;
    case 6: vm->map_x = (int8_t)(vm->map_x > 0 ? vm->map_x - 1 : 15); break;
    default: break;
    }
    vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
    vm->ahead = wall_after_step(game);
    vm->view_changed = true;
    log_position(game);
}

/* CALL (2fd3:329b) runs a routine of the game by its address:
 *
 * - 0x2e10, which scripts call after an encounter, recomputes the party's
 *   square and, if a picture, a sprite or the party's place or view
 *   changed, redraws the view and the status line and forgets them;
 * - 0xb203 plays sound 11 (DS:1e5e) if 0x3de (DS:8834) holds 10, else
 *   sound 10 (DS:1e5c);
 * - 0xc01e steps forward (3775:1bed);
 * - 0xc018 recomputes the wall ahead (69ea:06a2) outside 3D areas
 *   (0x4be6 0); with a facing other than 0, 2, 4 and 6 that is a byte of
 *   the stack that 3775:0032 left, unless it read a string, which depends
 *   on the opcodes before, and the run stops;
 * - 0x6803 draws the small picture's current frame at cell 3, 3
 *   (6961:000a), moves to the next, back to the first after the last, and
 *   waits speed * 100 ms (1521:0b4b); with the slot freed and nothing
 *   loaded since, the original draws from low memory, and the run stops;
 * - any other address does nothing. */
/* Show the view again if a picture or sprite covers it or the scripts
 * changed what it shows, and the status line (CALL [2e10]; dkk 3c1e:1dd9). */
static void redraw_changed(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
    if (!game->picture_shown && !game->sprite_shown && !vm->c059_changed &&
        !vm->view_changed && !vm->area_changed)
        return;
    game->sprite_loaded = game->closeup_shown = false;
    game->redraw = true;
    cok_adventure_view(game);
    cok_adventure_status(game);
    vm->area_changed = vm->c059_changed = vm->view_changed = false;
    game->picture_shown = game->sprite_shown = false;
    vm->ahead = cok_view_wall(&game->view, vm->direction, vm->map_x, vm->map_y);
}

static void call(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    switch (cok_ecl_call_of(vm->rules, cok_ecl_address(vm, 0))) {
    case COK_ECL_CALL_REDRAW: redraw_changed(game); break;
    case COK_ECL_CALL_SOUND:
        log_text(game, "sound", vm->value_3de == 10 ? "11" : "10"); /* 17e8:0020 */
        cok_adventure_sound(game, COK_SOUND_COMMAND, vm->value_3de == 10 ? 11 : 10);
        break;
    case COK_ECL_CALL_STEP: step_forward(game); break;
    case COK_ECL_CALL_AHEAD:
        if (cok_ecl_var(vm, COK_VAR_3D) != 0) break;
        if (!no_square(game) && !facing_known(vm->direction)) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "CALL [c018] facing %u: 69ea:06a2 returns a byte of the stack "
                               "the opcodes before left (2fd3:329b)", vm->direction);
            break;
        }
        vm->ahead = cok_view_wall(&game->view, vm->direction, vm->map_x, vm->map_y);
        break;
    case COK_ECL_CALL_ANIMATE:
        /* With the slot freed (6961:0537) and no picture loaded since,
         * DS:6da3 is 0, and entry 0 is the far pointer DS:6da2:6da0: a
         * segment of 0 (the count and frame) and as offset the segment of
         * the picture at DS:6d9e, the sky picture 252 loaded at startup
         * outside CGA mode (0 in CGA mode, which loads none): the original
         * draws from low memory. */
        if (game->frame_unset) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "CALL [6803] with no picture loaded draws entry 0 of DS:6da2, "
                               "0000:DS:6da0, from low memory (2fd3:33cb)");
            break;
        }
        /* The mouse's cursor hidden around the draw (1743:0051, 0030): the
         * delay after it waits with it shown, as here (the port composes
         * the cursor over the screen as it shows it). */
        draw_frame(game, game->frame);
        game->frame = game->frame + 1 < game->frame_count ? game->frame + 1 : 0;
        wait_ms(game, game->speed * 100u);
        break;
    default: break;
    }
}

/* Reset picture state for a new block, as 3775:01e8 does (DS:884a, 884c,
 * 8830). */
static void reset_pictures(cok_adventure *game)
{
    game->picture_shown = false;
    cok_monster_reset(game);
}

/* A script ended (2fd3:0050). The original clears DS:8830, 884a, 884c and
 * 8848 here. */
static void exited(cok_adventure *game)
{
    game->picture_shown = false;
    game->sprite_loaded = game->closeup_shown = false;
}

/* DESTROY ITEMS type (2fd3:35a3): every item of that type (+0x2e) of
 * every record in the list goes (6346:1697), unreadied first if readied
 * (546c:1ea7) with its record selected, so that the effect of its power
 * goes from it; a cursed one says "It's Cursed" and goes readied. Each
 * record's stats are then recomputed (6346:0d20), whether it lost any or
 * not, and the selection is restored. The original reads the next item
 * before it unreadies one: where the item's power takes items away or adds
 * them, the item or the next may be freed, and the run stops. */
static void destroy_items(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t type = (uint8_t)cok_ecl_value(vm, 0);
    uint8_t *saved = vm->character;
    for (size_t k = 0; k < game->party.count; ++k) {
        cok_character *c = game->party.members[k];
        for (size_t i = 0; i < c->item_count;) {
            if (c->items[i][0x2e] != type) {
                ++i;
                continue;
            }
            vm->character = c->record;
            size_t count = c->item_count;
            c->held = i + 1;
            if (c->items[i][0x34] != 0 && !cok_item_unready(game, c->items[i])) return;
            size_t held = c->held;
            c->held = 0;
            if (held != i + 1 || c->item_count != count) {
                cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                   "DESTROY ITEMS: an item's power changed the items it walks, "
                                   "which the original holds by pointers (2fd3:35a3)");
                return;
            }
            cok_character_remove_item(c, i);
        }
        char error[300];
        if (!cok_character_stats(c, cok_game_rules(game), &game->item_types, error, sizeof error)) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
            return;
        }
    }
    vm->character = saved;
}

/* FIND ITEM type (2fd3:2b7e): the flags say whether any record in the
 * list has an item of type (+0x2e): = if so, else <>, every other false. */
static void find_item(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t type = (uint8_t)cok_ecl_value(vm, 0);
    bool found = false;
    for (size_t k = 0; k < game->party.count && !found; ++k) {
        const cok_character *c = game->party.members[k];
        for (size_t i = 0; i < c->item_count && !found; ++i) found = c->items[i][0x2e] == type;
    }
    memset(vm->flags, 0, sizeof vm->flags);
    vm->flags[found ? 0 : 1] = true;
}

/* FIND SPECIAL id (2fd3:354f): the flags say whether the selected
 * character has effect id (6346:2447): = if so, else <>. With none
 * selected the original reads through NULL. */
static void find_special(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    memset(vm->flags, 0, sizeof vm->flags);
    const cok_character *c = member_of(game, vm->character);
    if (c == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "FIND SPECIAL with no character selected reads through NULL "
                           "(6346:2447)");
        return;
    }
    vm->flags[cok_character_find_effect(c, (uint8_t)cok_ecl_value(vm, 0)) != NULL ? 0 : 1] = true;
}

/* SPELL spell where who (2fd3:318b): the first record in the list with
 * spell among its 58 spell bytes (+0x1e-+0x57) stores the byte's index in
 * the second operand's variable and its position in the list in the
 * third's; with none, 0xff and the position of the last record (0 for an
 * empty list). Then the debug trace (6d7e:093e): "spell", the index, the
 * position and the spell. */
static void spell(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t wanted = (uint8_t)cok_ecl_value(vm, 0), index = 0xff, position = 0;
    bool found = false;
    for (size_t k = 0; k < game->party.count && !found; ++k) {
        const uint8_t *r = game->party.members[k]->record;
        for (uint8_t i = 0; i < cok_field_count(L, COK_FIELD_MEMORIZED) && !found; ++i) {
            found = cok_field_u8_at(L, r, COK_FIELD_MEMORIZED, i) == wanted;
            if (found) index = i;
        }
        if (!found && k + 1 < game->party.count) ++position;
    }
    cok_ecl_store(vm, cok_ecl_address(vm, 1), index);
    cok_ecl_store(vm, cok_ecl_address(vm, 2), position);
    cok_debug_trace(game, "spell", index, position, wanted); /* 2fd3:3292 */
}

/* ECL CLOCK count unit (2fd3:3005): pass the time (57e4:0549). */
static void ecl_clock(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_adventure_pass_time(game, (uint8_t)cok_ecl_value(vm, 1), (uint8_t)cok_ecl_value(vm, 0));
}

/* Add monster id of the ECL file to the party as an NPC (4b6d:18fc), if
 * the party (0x7f3e) has at most seven: read as LOAD MONSTER reads it
 * (4b6d:161b), its id kept in +0x115, then joined as a saved game's
 * characters join (4b6d:1989): after every record in the list, in the
 * lowest icon slot 0-7 free (8 if none is), selected, counted in 0x7f3e,
 * its levels recomputed (66c2:0433) if it is an NPC (+0xe7 0x80 and up);
 * then its combat icons, CPIC<file> records id and id + 0x80, loaded in
 * its slot (6d21:01d0). */
static bool join_npc(cok_adventure *game, uint8_t id)
{
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_character *npc = malloc(sizeof *npc);
    if (npc == NULL) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "out of memory");
        return false;
    }
    if (!cok_monster_read(game, id, npc)) {
        cok_character_free(npc);
        free(npc);
        return false;
    }
    cok_field_set8(L, npc->record, COK_FIELD_MONSTER_ID, id);
    if (!cok_party_add(&game->party, cok_game_rules(game), npc)) {
        cok_character_free(npc);
        free(npc);
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "the party list holds %u records; combat's tables hold no more",
                           COK_PARTY_RECORDS);
        return false;
    }
    vm->character = npc->record;
    cok_ecl_add_var(vm, COK_VAR_PARTY_SIZE, 1);
    char error[300];
    if (cok_field_u8(L, npc->record, COK_FIELD_NPC) >= 0x80 &&
        !cok_character_levels(npc, cok_game_rules(game), &game->item_types, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    char name[16], text[64];
    name_of(L, npc->record, name);
    uint8_t slot = cok_field_u8(L, npc->record, COK_FIELD_ICON);
    snprintf(text, sizeof text, "%s joins, icon %u in slot %u", name, id, slot);
    log_text(game, "party", text);
    return cok_arena_load_icon(game, cok_game_rules(game)->archives.icons, id, slot);
}

/* ADD NPC id morale (2fd3:311c): unless the party has eight or more, add
 * monster id as an NPC, which is then selected (join_npc); then the
 * selected character, whoever it is, gets +0xe7 0x80 + morale / 2, its
 * stats are recomputed (6346:0d20) and the party list drawn. With the
 * party full and none selected the original writes through NULL. */
static void add_npc(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t id = (uint8_t)cok_ecl_value(vm, 0);
    if (cok_ecl_var(vm, COK_VAR_PARTY_SIZE) <= 7 && !join_npc(game, id)) return;
    cok_character *c = member_of(game, vm->character);
    if (c == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "ADD NPC with the party full and no character selected writes "
                           "through NULL (2fd3:311c)");
        return;
    }
    cok_field_set8(cok_game_rules(game)->character, c->record, COK_FIELD_NPC,
                   (uint8_t)((uint8_t)cok_ecl_value(vm, 1) >> 1 | 0x80));
    char error[300];
    if (!cok_character_stats(c, cok_game_rules(game), &game->item_types, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return;
    }
    cok_adventure_party(game);
}

/* DUMP (2fd3:351b): remove the selected character from the list
 * (4def:3b0a with 0, 1: its icons freed, counted out of 0x7f3e) and select
 * the record before it, or the first; with none selected, select the
 * first. The selection becomes the one EXIT restores after LOAD CHARACTER
 * (DS:43bf), and the party list is drawn. */
static void dump(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    size_t i = cok_party_index(&game->party, vm->character);
    if (i < game->party.count) {
        char name[16], text[32];
        name_of(cok_game_rules(game)->character, vm->character, name);
        snprintf(text, sizeof text, "%s leaves", name);
        log_text(game, "party", text);
        /* DS:43bf is set from the new selection below. */
        vm->saved_character = NULL;
        if (!cok_treasure_remove_record(game, i, false, true, false)) return;
    } else if (game->party.count > 0) {
        vm->character = game->party.members[0]->record;
    }
    vm->saved_character = vm->character;
    cok_adventure_party(game);
}

static cok_ecl_status camp(cok_adventure *game, uint16_t sp);

/* PROGRAM (2fd3:3473; dkk 3632:23dd): with Move mode off meanwhile (DS:8858), and the
 * selection LOAD CHARACTER changed restored first (DS:43ba), 9 camps as
 * Encamp does (2fd3:3403), in the middle of the script, which goes on from
 * the instruction after (DS:4b43 kept), and then ends as EXIT does unless
 * 0x4c38 is set; 0 opens the start menu (4def:01b4, see start.h), where
 * the training halls' scripts have set var 0x7ea8 to allow training, and
 * the script goes on once Begin Adventuring leaves it; other values do
 * nothing. */
static void program(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    bool moving = game->moving;
    game->moving = false;
    if (vm->restore_character) {
        vm->character = vm->saved_character;
        vm->restore_character = false;
    }
    uint8_t what = (uint8_t)cok_ecl_value(vm, 0);
    if (what == 0) {
        /* The start menu, then the screen redrawn for the mode, but in 3D
         * areas, with 0x4c38 set or picture 9 loaded. */
        uint16_t ip = vm->ip;
        cok_start_menu(game);
        vm->ip = ip;
        /* dkk 3632:23dd: not once the menu ended the game (DS:8c5b), and
         * the party marked on the map again (6997:0000). */
        if (!vm->abort && !game->new_game && game->picture_id != 9 &&
            cok_ecl_var(vm, COK_VAR_3D) == 0 && !cok_ecl_flag(vm, COK_VAR_OFF_MAP)) {
            cok_adventure_redraw(game);
            if (cok_game_rules(game)->start.program_marks) cok_adventure_mark(game);
        }
    } else if (what == 9) {
        uint16_t ip = vm->ip;
        /* Its vectors run 0x128 bytes below the script's (2fd3:3473 ->
         * 3403 -> 3add). */
        cok_ecl_status status =
            camp(game, (uint16_t)(game->battle_sp - COK_PROGRAM_CAMP_DEPTH));
        vm->ip = ip;
        if (status != COK_ECL_OK) {
            vm->status = status;
        } else if (!vm->abort && !cok_ecl_flag(vm, COK_VAR_OFF_MAP)) {
            cok_ecl_exit(vm);
            exited(game);
        }
    }
    game->moving = moving;
}

/* A later game's opcodes (ecl.h's meanings past Champions'), as Death
 * Knights' handlers do them. */

/* PRESS KEY (dkk 3632:1b5f): a menu of one item, which Enter takes. */
static void press_key(cok_adventure *game)
{
    int choice = cok_adventure_horizontal(game, "", "~Press button or enter to continue", 10,
                                          true);
    if (choice >= 0) cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 6a69:145d */
}

/* JOURNAL n (dkk 3632:279f): the text window cleared for "This is recorded
 * as journal entry n." (DS:2a79), then PRESS KEY. */
static void journal(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    char text[64];
    snprintf(text, sizeof text, "This is recorded as journal entry %u.",
             (unsigned)(uint8_t)cok_ecl_value(vm, 0));
    vm->cursor = (cok_text_cursor){1, 0x11};
    log_text(game, "print", text);
    game->text_shown = true;
    cok_adventure_wrap(game, &vm->cursor, text, cok_adventure_text_window, 10, true, page);
    if (!game->input_ended) press_key(game);
}

/* YES NO (dkk 3632:1b9e): a menu of Yes and No (6a69:2048, the items at
 * DS:31cb, no prompt); = if Yes (DS:57c0's 0), else <>. */
static void yes_no(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    memset(vm->flags, 0, sizeof vm->flags);
    game->selected = 1;
    int choice = cok_adventure_horizontal(game, "", "~Yes ~No", 10, false);
    if (choice < 0) return;
    char text[16];
    snprintf(text, sizeof text, "%d", choice);
    log_text(game, "choice", text);
    vm->flags[choice == 0 ? 0 : 1] = true;
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
}

/* CLEAR EXIT (dkk 3632:2859): the text window cleared (14e4:0b6b), what
 * changed shown again (3c1e:1dd9), and EXIT. */
static void clear_exit(cok_adventure *game)
{
    cok_picture_fill(&game->screen, 1, 0x11 * 8, 0x26, 6 * 8, 0);
    redraw_changed(game);
    cok_ecl_exit(&game->vm);
    exited(game);
}

/* ASK MENU (dkk 3632:11fd with 1): "what do you do" (DS:2bed) in the text
 * window where the text stands, then HORIZONTAL MENU. */
static void ask_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    static const char question[] = "what do you do";
    log_text(game, "print", question);
    game->text_shown = true;
    cok_adventure_wrap(game, &vm->cursor, question, cok_adventure_text_window, 10, false, page);
    if (!game->input_ended) horizontal_menu(game);
}

/* MUSIC n (dkk 3632:2878): a song, which the port does not play yet. */
static void music(cok_adventure *game)
{
    char text[16];
    snprintf(text, sizeof text, "%u", (unsigned)(uint8_t)cok_ecl_value(&game->vm, 0));
    log_text(game, "music", text);
}

/* An opcode the port does not carry out yet: the run stops. */
static void not_yet(cok_adventure *game, const char *routine)
{
    cok_adventure_fail(game, COK_ECL_UNPORTED, "not yet ported: %s's opcode 0x%02x, %s (%s)",
                       cok_game_rules(game)->text.name, game->vm.opcode,
                       cok_ecl_opcode_name(game->vm.rules, game->vm.opcode), routine);
}

static void opcode(cok_ecl *vm, void *context)
{
    cok_adventure *game = context;
    const char *unported = cok_ecl_unported(vm->rules, vm->opcode);
    if (unported != NULL) {
        not_yet(game, unported);
        return;
    }
    switch (cok_ecl_meaning(vm)) {
    case COK_ECL_EXIT: case COK_ECL_RETURN: exited(game); break;
    case COK_ECL_NEWECL: reset_pictures(game); break;
    case COK_ECL_PRINT: case COK_ECL_PRINTCLEAR: print(game); break;
    case COK_ECL_PRINT_RETURN: break;
    case COK_ECL_HORIZONTAL_MENU: horizontal_menu(game); break;
    case COK_ECL_VERTICAL_MENU: vertical_menu(game); break;
    case COK_ECL_INPUT_NUMBER: case COK_ECL_INPUT_STRING: input(game); break;
    case COK_ECL_PICTURE: picture(game); break;
    case COK_ECL_CLEAR_BOX: clear_box(game); break;
    case COK_ECL_DELAY: {
        unsigned fixed = cok_game_rules(game)->ecl.delay_ms; /* dkk 3632:1c90 */
        wait_ms(game, fixed != 0 ? fixed : game->speed * 100u);
        break;
    }
    case COK_ECL_LOAD_FILES: case COK_ECL_LOAD_PIECES: load_files(game); break;
    case COK_ECL_LOAD_CHARACTER: load_character(game); break;
    case COK_ECL_ADD_EP: add_experience(game); break;
    case COK_ECL_WHO: who(game); break;
    case COK_ECL_DAMAGE: damage(game); break;
    case COK_ECL_CALL: call(game); break;
    case COK_ECL_PROGRAM: program(game); break;
    case COK_ECL_DESTROY_ITEMS: destroy_items(game); break;
    case COK_ECL_FIND_ITEM: find_item(game); break;
    case COK_ECL_FIND_SPECIAL: find_special(game); break;
    case COK_ECL_SPELL: spell(game); break;
    case COK_ECL_ECL_CLOCK: ecl_clock(game); break;
    case COK_ECL_ADD_NPC: add_npc(game); break;
    case COK_ECL_DUMP: dump(game); break;
    case COK_ECL_PRESS_KEY: press_key(game); break;
    case COK_ECL_JOURNAL: journal(game); break;
    case COK_ECL_YES_NO: yes_no(game); break;
    case COK_ECL_CLEAR_EXIT: clear_exit(game); break;
    case COK_ECL_ASK_MENU: ask_menu(game); break;
    case COK_ECL_MUSIC: music(game); break;
    case COK_ECL_NOTHING: break;
    default:
        if (cok_monster_opcode(game) || cok_treasure_opcode(game)) break;
        if (game->hooks.unported != NULL) game->hooks.unported(game, game->hooks.context);
        break;
    }
}

/* 3775:0361: row 24 cleared and "Loading...Please Wait" shown while the
 * block is read, then the row cleared again (67b5:0c7b). The original
 * reads again for ever while the record is shorter than two bytes; the
 * port stops. */
static bool load_block(cok_ecl *vm, uint8_t block, void *context)
{
    cok_adventure *game = context;
    cok_adventure_loading(game); /* 3775:037f */
    game->error[0] = '\0';
    size_t size;
    const char *name = cok_game_rules(game)->archives.ecl;
    uint8_t *data = cok_adventure_record(game, name, vm->file, block, &size);
    if (data == NULL) return false;
    cok_ecl_status status = cok_ecl_load(vm, data, size);
    free(data);
    if (status != COK_ECL_OK) {
        fail(game, "%s%u.DAX record %u: %s", name, vm->file, block,
             cok_ecl_status_string(status));
        return false;
    }
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 3775:042c */
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

/* 2fd3:3b1d: the debug trace of the opcode run, by its name (6d7e:024f),
 * with three zeros. */
static void executed(cok_ecl *vm, void *context)
{
    cok_adventure *game = context;
    if (!game->debug) return;
    char name[16];
    cok_debug_opcode_name(vm->rules, vm->opcode, name);
    cok_debug_trace(game, name, 0, 0, 0);
}

/* The effects' handlers speak in the text window, or in combat the side
 * panel (6346:1883, on row 10 with a pause), or with a flash on the
 * character (6346:228c), after which 60f4:20f7 clears the text
 * (6346:196a). */
static void effect_say(cok_effects *fx, cok_character *c, const char *text, bool wait,
                       void *context)
{
    (void)fx;
    (void)cok_arena_say(context, c, text, 10, wait); /* row 10 is in the panel */
}

static bool effect_flash(cok_effects *fx, cok_character *c, uint8_t kind, const char *text,
                         bool clear, void *context)
{
    cok_adventure *game = context;
    if (!cok_arena_flash(game, c, kind, text)) {
        /* The reason, for the effect's failure. */
        snprintf(fx->error, sizeof fx->error, "%.299s", game->error);
        fx->failed = true;
        return false;
    }
    if (clear) cok_arena_clear_text(game);
    return true;
}

static bool effect_panel(cok_effects *fx, cok_character *c, void *context)
{
    cok_adventure *game = context;
    if (cok_arena_panel(game, c)) return true;
    snprintf(fx->error, sizeof fx->error, "%.299s", game->error);
    fx->failed = true;
    return false;
}

/* The battle's side of the handlers ported with the attacks. A failure
 * has ended the run; the effect's failure says why. */
static bool effect_hook_failed(cok_effects *fx, cok_adventure *game)
{
    snprintf(fx->error, sizeof fx->error, "%.299s", game->error);
    fx->failed = true;
    return false;
}

static bool effect_count_sides(cok_effects *fx, void *context)
{
    cok_adventure *game = context;
    if (cok_combat_count_sides(&game->combat, &game->party, cok_game_rules(game))) return true;
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "a record on a side other than 0 or 1 (+0x18a) "
                                                "counts past DS:6b2d (6346:268a)");
    return effect_hook_failed(fx, game);
}

static bool effect_damage(cok_effects *fx, cok_character *c, uint8_t amount, uint8_t save_kind,
                          bool saved, void *context)
{
    cok_adventure *game = context;
    return cok_cast_damage(game, c, amount, save_kind, saved) || effect_hook_failed(fx, game);
}

static bool effect_reflect(cok_effects *fx, cok_character *c, cok_character **caster,
                           char *error, size_t error_size, void *context)
{
    (void)error, (void)error_size;
    cok_adventure *game = context;
    return cok_cast_reflect(game, c, caster) || effect_hook_failed(fx, game);
}

static bool effect_kill(cok_effects *fx, cok_character *c, uint8_t status, const char *text,
                        void *context)
{
    cok_adventure *game = context;
    return cok_combat_kill(game, c, status, text) || effect_hook_failed(fx, game);
}

static bool effect_revive(cok_effects *fx, cok_character *c, uint8_t hp, const char *text,
                          bool *placed, void *context)
{
    cok_adventure *game = context;
    return cok_combat_revive(game, c, hp, text, placed) || effect_hook_failed(fx, game);
}

/* 3f44:1f97's list of the dead that explode (DS:6b95 a byte), or
 * DS:6b96. */
static bool effect_exploding(cok_effects *fx, cok_character *c, bool *now, void *context)
{
    (void)fx;
    cok_combat *combat = &((cok_adventure *)context)->combat;
    *now = combat->exploding_now;
    if (c == NULL) return true;
    ++combat->exploding_count;
    if (combat->exploding_count < 1) combat->exploding_count = 1;
    if (combat->exploding_count <= 20)
        combat->exploding[combat->exploding_count] = c;
    else
        combat->exploding_count = 20;
    return true;
}

/* 3f44:1dc5's loss: unreadied (546c:1ea7, the item not cursed), copied
 * with its owner after the weapons lost in combat (DS:609e), removed
 * (6346:1697), the weapon slot cleared and the panel drawn. */
static bool effect_lose_weapon(cok_effects *fx, cok_character *killer, size_t item,
                               void *context)
{
    cok_adventure *game = context;
    uint8_t *it = killer->items[item - 1];
    if (!cok_item_unready(game, it)) return effect_hook_failed(fx, game);
    cok_lost_weapon *more =
        realloc(game->lost_weapons, (game->lost_weapon_count + 1) * sizeof *more);
    if (more == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory");
        return effect_hook_failed(fx, game);
    }
    game->lost_weapons = more;
    cok_lost_weapon *lost = &more[game->lost_weapon_count++];
    memcpy(lost->item, it, sizeof lost->item);
    lost->owner = killer->record;
    cok_character_remove_item(killer, item - 1);
    killer->slots[0] = 0;
    /* 6346:0af6, which draws only while DS:71ac is set. */
    return cok_arena_panel(game, killer) || effect_hook_failed(fx, game);
}

static bool effect_explode(cok_effects *fx, void *context)
{
    cok_adventure *game = context;
    return cok_combat_explode(game) || effect_hook_failed(fx, game);
}

static bool effect_gate(cok_effects *fx, cok_character *c, void *context)
{
    cok_adventure *game = context;
    return cok_combat_gate(game, c) || effect_hook_failed(fx, game);
}

static bool effect_clouds(cok_effects *fx, uint8_t id, cok_effect *effect, cok_character *c,
                          void *context)
{
    cok_adventure *game = context;
    const cok_rules *rules = cok_game_rules(game);
    bool ok = cok_effect_is(rules, id, COK_EFFECT_CLOUD)        ? cok_cloud_clear(game, c, effect)
              : cok_effect_is(rules, id, COK_EFFECT_ACID_DEATH) ? cok_cloud_acid(game, c)
                                                                 : cok_cloud_drain(game, c, effect);
    return ok || effect_hook_failed(fx, game);
}

static bool effect_breathe(cok_effects *fx, cok_effect *effect, cok_character *c, void *context)
{
    cok_adventure *game = context;
    return cok_cast_breath(game, c, effect) || effect_hook_failed(fx, game);
}

static bool effect_dispel(cok_effects *fx, void *context)
{
    cok_adventure *game = context;
    return cok_cast_dispel(game) || effect_hook_failed(fx, game);
}

static void effect_party_list(cok_effects *fx, void *context)
{
    (void)fx;
    cok_adventure_party(context);
}

/* 6b30:08d8 around c with any range: the second listed (DS:6a36). */
static bool effect_second(cok_effects *fx, cok_character *c, uint8_t **record, void *context)
{
    cok_adventure *game = context;
    cok_combat *combat = &game->combat;
    const uint8_t *r = c->record;
    *record = NULL;
    if (!combat->active) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "the combatants around one (6b30:08d8) are "
                                                    "listed with no combat map");
        return effect_hook_failed(fx, game);
    }
    if (!cok_combat_list(combat, cok_game_rules(game), cok_combat_x(combat, r),
                         cok_combat_y(combat, r), 0xff, 0xff,
                         cok_combat_size(combat, r))) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "the list of combatants around one "
                                                    "(6b30:08d8) reads past its tables");
        return effect_hook_failed(fx, game);
    }
    uint8_t n = combat->listed[2].index;
    const cok_character *second = n <= COK_COMBATANTS ? combat->combatant[n].character : NULL;
    *record = second != NULL ? (uint8_t *)second->record : NULL;
    return true;
}

static void effect_log(cok_effects *fx, const char *kind, const char *text, void *context)
{
    (void)fx;
    cok_adventure_log(context, kind, text);
}

/* 60f4:0352 in combat: whether target is among the combatants listed
 * within radius of holder (6b30:08d8); the list is put back after, but
 * for its count. */
static bool effect_in_range(cok_effects *fx, cok_character *holder, cok_character *target,
                            uint8_t radius, bool *in, char *error, size_t error_size,
                            void *context)
{
    (void)fx;
    cok_adventure *game = context;
    cok_combat *combat = &game->combat;
    cok_combat_listing saved[COK_COMBATANTS + 1];
    memcpy(saved, combat->listed, sizeof saved);
    const uint8_t *r = holder->record;
    bool ok = cok_combat_list(combat, cok_game_rules(game), cok_combat_x(combat, r),
                              cok_combat_y(combat, r), radius,
                              0xff, cok_combat_size(combat, r));
    *in = false;
    uint8_t n = cok_combat_index(combat, target->record);
    for (unsigned i = 1; ok && i <= combat->listed_count; ++i)
        if (combat->listed[i].index == n) *in = true;
    memcpy(&combat->listed[1], &saved[1], sizeof saved - sizeof saved[0]);
    if (!ok)
        snprintf(error, error_size, "the list of combatants around one (6b30:08d8) reads past "
                                    "its tables");
    return ok;
}

static bool effect_listed(cok_effects *fx, cok_character **listed, uint8_t *count, char *error,
                          size_t error_size, void *context);

/* 6b30:08d8 around c for a handler, the list kept as it leaves it. */
static bool effect_around(cok_effects *fx, cok_character *c, uint8_t radius,
                          cok_character **listed, uint8_t *count, char *error,
                          size_t error_size, void *context)
{
    (void)fx;
    cok_adventure *game = context;
    cok_combat *combat = &game->combat;
    const uint8_t *r = c->record;
    *count = 0;
    if (!combat->active) {
        snprintf(error, error_size, "the combatants around one (6b30:08d8) are listed with no "
                                    "combat map");
        return false;
    }
    if (!cok_combat_list(combat, cok_game_rules(game), cok_combat_x(combat, r),
                         cok_combat_y(combat, r), radius, 0xff,
                         cok_combat_size(combat, r))) {
        snprintf(error, error_size, "the list of combatants around one (6b30:08d8) reads past "
                                    "its tables");
        return false;
    }
    return effect_listed(fx, listed, count, error, error_size, context);
}

/* The list as it is now (DS:6a30, count DS:6a32), not made again. */
static bool effect_listed(cok_effects *fx, cok_character **listed, uint8_t *count, char *error,
                          size_t error_size, void *context)
{
    (void)fx;
    cok_adventure *game = context;
    cok_combat *combat = &game->combat;
    *count = 0;
    if (!combat->active) {
        snprintf(error, error_size, "the combatants listed (DS:6a30) are read with no combat map");
        return false;
    }
    for (unsigned i = 1; i <= combat->listed_count; ++i) {
        uint8_t n = combat->listed[i].index;
        listed[i] = n <= COK_COMBATANTS ? combat->combatant[n].character : NULL;
        if (listed[i] == NULL) {
            snprintf(error, error_size, "a listed combatant has no record (6b30:08d8)");
            return false;
        }
    }
    *count = combat->listed_count;
    return true;
}

static bool effect_distance(cok_effects *fx, const uint8_t *origin, const uint8_t *target,
                            uint8_t *distance, char *error, size_t error_size, void *context)
{
    (void)fx;
    cok_adventure *game = context;
    if (!game->combat.active) {
        snprintf(error, error_size, "the distance (6346:2888) is worked out with no combat map");
        return false;
    }
    if (cok_combat_distance(&game->combat, cok_game_rules(game), origin, target, distance))
        return true;
    snprintf(error, error_size, "the distance (6346:2888) lists the combatants around one "
                                "(6b30:08d8) past its tables");
    return false;
}

static void effect_trace(cok_effects *fx, const char *text, uint8_t a, uint8_t b, uint8_t c,
                         void *context)
{
    (void)fx;
    cok_debug_trace(context, text, a, b, c);
}

bool cok_adventure_open(cok_adventure *game, const cok_rules *rules, const char *assets,
                        const cok_keyboard *keys, const cok_adventure_hooks *hooks)
{
    return cok_adventure_open_display(game, rules, assets, COK_DISPLAY_TANDY, keys, hooks);
}

bool cok_adventure_open_display(cok_adventure *game, const cok_rules *rules, const char *assets,
                                cok_display display, const cok_keyboard *keys,
                                const cok_adventure_hooks *hooks)
{
    assert(rules != NULL);
    memset(game, 0, sizeof *game);
    game->rules = rules;
    game->display = display;
    game->battle_sp = COK_BATTLE_SP;
    cok_ecl_hooks vm_hooks = {.load = load_block, .opcode = opcode, .trace = trace,
                              .executed = executed, .character_value = character_value,
                              .context = game};
    cok_ecl_init(&game->vm, cok_game_rules(game), &vm_hooks);
    cok_effects_init(&game->effects, cok_game_rules(game), &game->vm, &game->party,
                     &game->item_types);
    game->effects.say = effect_say;
    game->effects.log = effect_log;
    game->effects.in_range = effect_in_range;
    game->effects.around = effect_around;
    game->effects.listed = effect_listed;
    game->effects.distance = effect_distance;
    game->effects.flash = effect_flash;
    game->effects.panel = effect_panel;
    game->effects.count_sides = effect_count_sides;
    game->effects.damage = effect_damage;
    game->effects.kill = effect_kill;
    game->effects.reflect = effect_reflect;
    game->effects.revive = effect_revive;
    game->effects.exploding = effect_exploding;
    game->effects.lose_weapon = effect_lose_weapon;
    game->effects.second = effect_second;
    game->effects.explode = effect_explode;
    game->effects.gate = effect_gate;
    game->effects.clouds = effect_clouds;
    game->effects.breathe = effect_breathe;
    game->effects.dispel = effect_dispel;
    game->effects.party_list = effect_party_list;
    game->effects.trace = effect_trace;
    game->effects.context = game;
    game->vm.file = 1;
    game->picture_id = COK_ADVENTURE_NO_PICTURE;
    game->big_id = COK_ADVENTURE_NO_PICTURE;
    game->head_id = game->body_id = 0xff; /* 3e99:0477 */
    game->frame_unset = true;             /* DS:6da3 0 */
    game->speed = 4;
    game->text_delay = true; /* 3e99:0560 */
    for (size_t i = 0; i < COK_VIEW_WALL_SLOTS; ++i) game->wall_ids[i] = game->wall_slots[i] = -1;
    game->wall_ids[0] = 0; /* 3e99:005b */
    game->wall_slots[0] = 1;
    game->view.shape = &rules->view;
    game->animate = true;
    game->pictures = 1;
    game->selected = 1;
    game->text_shown = true;
    game->icon_slot = 8;
    game->vault_letter = 'Q'; /* dkk 4f9a:00e1 */
    cok_adventure_pointer_init(game);
    if (keys != NULL) game->keys = *keys;
    if (hooks != NULL) game->hooks = *hooks;
    if (strlen(assets) >= sizeof game->assets) {
        fail(game, "asset path too long");
        return false;
    }
    snprintf(game->assets, sizeof game->assets, "%s", assets);
    if (cok_adventure_create_picture(game, &game->screen, 40, 200, 1, 0) != COK_PICTURE_OK) {
        fail(game, "out of memory");
        return false;
    }
    const cok_rules_archives *a = &rules->archives;
    char name[32];
    record_file(&a->font, name, sizeof name);
    size_t size;
    uint8_t *data = read_record(game, name, a->font.record, &size);
    if (data == NULL) return false;
    cok_picture_status status = cok_font_load(&game->font, data, size);
    free(data);
    if (status != COK_PICTURE_OK) {
        fail(game, "%s.DAX record %u: %s", name, a->font.record,
             cok_picture_status_string(status));
        return false;
    }
    /* The game loads these once at startup, while the ECL file is still 1
     * (3e99:005b): the frame's tile set and tile set 0 (Champions'; the
     * descriptor's startup), and outside CGA mode the sky pictures. */
    const cok_rules_startup *at_start = &rules->startup;
    for (size_t i = 0; i < at_start->tile_count; ++i)
        if (!load_tiles(game, at_start->tiles[i].set, at_start->tiles[i].record)) return false;
    /* Then the icons of missiles, flashes and the cursor: in Champions
     * COMSPR ids 0-11 in slots 13-24, and 25 in 25 (3e99:06e0, 072a); in
     * Death Knights 0-21 in 13-34 (dkk 4f9a:00e1). */
    for (size_t r = 0; r < at_start->icon_ranges; ++r)
        for (uint8_t i = 0; i < at_start->icons[r].count; ++i)
            if (!cok_arena_load_icon(game, a->missiles, (uint8_t)(at_start->icons[r].first + i),
                                     (uint8_t)(at_start->icons[r].slot + i)))
                return false;
    for (uint8_t i = 0; a->sky != NULL && i < COK_VIEW_SKY_PICTURES && display != COK_DISPLAY_CGA;
         ++i)
        if (!load_single(game, a->sky, (uint8_t)(250 + i), 13, &game->view.sky[i])) return false;
    /* A game with no sky has the night sky, SKYGRND 1, and the ground by
     * day, 2, loaded without a mask (dkk 4f9a:00e1, DS:42d0's 0). */
    if (a->ground != NULL &&
        (!load_single(game, a->ground, 1, -1, &game->view.ground[1]) ||
         !load_single(game, a->ground, 2, -1, &game->view.ground[0])))
        return false;
    /* The overland map's cursor and the cell it covers (3e99:01fd, 07b8). */
    record_file(&a->cursor, name, sizeof name);
    if (!load_single(game, name, a->cursor.record, at_start->cursor_transparent, &game->cursor))
        return false;
    if (cok_adventure_create_picture(game, &game->under, 1, 8, 1, 0) != COK_PICTURE_OK) {
        fail(game, "out of memory");
        return false;
    }
    /* 3e99:005b also reads the item types. */
    char path[sizeof game->assets + 32];
    snprintf(path, sizeof path, "%s/%s", game->assets, rules->files.item_types);
    return cok_item_types_read(path, &game->item_types, game->error, sizeof game->error);
}

void cok_adventure_close(cok_adventure *game)
{
    cok_debug_end(game);
    cok_party_free(&game->party);
    cok_effects_free(&game->effects);
    game->vm.character = NULL;
    free_frames(game);
    cok_picture_free(&game->big);
    cok_picture_free(&game->head);
    cok_picture_free(&game->body);
    cok_picture_free(&game->cursor);
    cok_picture_free(&game->under);
    for (size_t i = 0; i < COK_ICON_SLOTS_MAX; ++i)
        for (size_t pose = 0; pose < 2; ++pose) cok_picture_free(&game->icons[i][pose]);
    cok_pool_free(&game->pool);
    cok_cloud_free(&game->combat);
    cok_picture_free(&game->combat.tiles);
    cok_picture_free(&game->combat.buffer);
    cok_picture_free(&game->combat.under);
    cok_picture_free(&game->combat.flash);
    free(game->lost_weapons);
    game->lost_weapons = NULL;
    game->lost_weapon_count = 0;
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

/* Add character file base.extension from dir to the party, as 4b6d:1b34
 * adds a saved game's: read (4b6d:11e5), joined (4b6d:1989), its levels
 * recomputed if it is an NPC, counted in 0x7f3e. */
static bool add_character(cok_adventure *game, const char *dir, const char *base,
                          const char *extension)
{
    cok_ecl *vm = &game->vm;
    char file[4200];
    snprintf(file, sizeof file, "%s/%s.%s", dir, base, extension);
    cok_character *character = malloc(sizeof *character);
    if (character == NULL) {
        fail(game, "out of memory");
        return false;
    }
    if (!cok_character_read_file(character, cok_game_rules(game), dir, base, extension,
                                 &game->item_types, game->error, sizeof game->error)) {
        cok_character_free(character);
        free(character);
        return false;
    }
    cok_adventure_flush(game); /* 4b6d:15ce, in 11e5 before the recompute */
    if (!cok_party_add(&game->party, cok_game_rules(game), character)) {
        cok_character_free(character);
        free(character);
        fail(game, "%s: the party is full", file);
        return false;
    }
    /* 4b6d:1989 recomputes an NPC's levels once it has joined. */
    if (cok_field_u8(cok_game_rules(game)->character, character->record, COK_FIELD_NPC) >= 0x80 &&
        !cok_character_levels(character, cok_game_rules(game), &game->item_types, game->error,
                              sizeof game->error))
        return false;
    cok_ecl_add_var(vm, COK_VAR_PARTY_SIZE, 1);
    return true;
}

/* Add the characters a saved game names, from its directory. */
static bool add_characters(cok_adventure *game, const cok_saved_game *saved, const char *path,
                           uint8_t ecl_file)
{
    cok_ecl *vm = &game->vm;
    char dir[4096];
    directory_of(path, dir, sizeof dir);
    for (size_t i = 0; i < saved->count && i < COK_PARTY_MAX; ++i) {
        char base[9];
        cok_party_roster_name(saved->names[i], base);
        char file[4200];
        snprintf(file, sizeof file, "%s/%s.%s", dir, base, cok_game_rules(game)->saves.record);
        FILE *exists = fopen(file, "rb");
        if (exists == NULL) continue; /* the original skips missing characters */
        fclose(exists);
        if (!add_character(game, dir, base, cok_game_rules(game)->saves.record)) return false;
    }
    /* Then every record's combat icons, an NPC's from CPIC of the saved
     * game's file (4b6d:1fff). */
    if (!cok_arena_join(game, ecl_file)) return false;
    /* Adding a character selects it; the loader then selects the first. */
    vm->character = cok_party_record(&game->party, 0);
    return true;
}

bool cok_adventure_add_characters(cok_adventure *game, const char *const *paths, size_t count)
{
    cok_ecl *vm = &game->vm;
    game->error[0] = '\0';
    for (size_t i = 0; i < count; ++i) {
        char dir[4096], base[4096];
        directory_of(paths[i], dir, sizeof dir);
        const char *name = strrchr(paths[i], '/');
        snprintf(base, sizeof base, "%s", name != NULL ? name + 1 : paths[i]);
        char *dot = strrchr(base, '.');
        if (dot == NULL || dot == base) {
            fail(game, "%s: a character's file has an extension", paths[i]);
            return false;
        }
        *dot = '\0';
        if (!add_character(game, dir, base, dot + 1)) return false;
    }
    if (!cok_arena_join(game, vm->file)) return false;
    vm->character = cok_party_record(&game->party, 0);
    return true;
}

bool cok_adventure_load_party(cok_adventure *game, const char *path)
{
    static cok_saved_game saved;
    game->error[0] = '\0';
    if (!cok_saved_game_read(path, cok_game_rules(game), &saved, game->error, sizeof game->error))
        return false;
    return add_characters(game, &saved, path, game->vm.file);
}

/* The saved game's letter: the last character of its file's name before
 * the extension, upper-cased (the letter the loader was given). */
static char saved_letter(const char *path)
{
    const char *name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;
    const char *dot = strrchr(name, '.');
    size_t length = dot != NULL ? (size_t)(dot - name) : strlen(name);
    char c = length > 0 ? name[length - 1] : '\0';
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

/* What both loaders set from the variables once they are read (4b6d:1b34,
 * dkk 46b2:1ef0): the animation, the pictures and the speed (DS:4b4f, 4b4d,
 * 4b38), and no party yet in 0x7f3e. */
static void loaded_vars(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    game->animate = (cok_ecl_var(vm, COK_VAR_PICTURES) & 1) != 0; /* DS:4b4f */
    game->pictures = (uint8_t)(cok_ecl_var(vm, COK_VAR_PICTURES) >> 1); /* DS:4b4d */
    game->speed = (uint8_t)cok_ecl_var(vm, COK_VAR_SPEED);
    cok_ecl_set_var(vm, COK_VAR_PARTY_SIZE, 0);
}

bool cok_adventure_restore(cok_adventure *game, const char *path)
{
    static cok_saved_game saved;
    cok_ecl *vm = &game->vm;
    const cok_rules *rules = cok_game_rules(game);
    game->error[0] = '\0';
    if (!cok_saved_game_read(path, rules, &saved, game->error, sizeof game->error)) return false;
    vm->keep_vars = true;
    memcpy(vm->mem4b00, saved.mem4b00, sizeof vm->mem4b00);
    memcpy(vm->mem7c00, saved.mem7c00, sizeof vm->mem7c00);
    memcpy(vm->mem7a00, saved.mem7a00, sizeof vm->mem7a00);
    vm->map_x = saved.map_x;
    vm->map_y = saved.map_y;
    vm->direction = saved.direction;
    vm->ahead = saved.ahead;
    vm->square = saved.square;
    loaded_vars(game);
    /* dkk 46b2:1ef0: the vault is the saved game's letter's (DS:fb79). */
    if (rules->saves.vault != NULL) game->vault_letter = saved_letter(path);
    if (!add_characters(game, &saved, path, saved.file)) return false;
    /* The save holds the ECL file twice; the loader takes it from 0x7f12. */
    vm->file = (uint8_t)cok_ecl_var(vm, COK_VAR_ECL_FILE);
    /* The loader reads DS:6d8a as saved, then reloads what it names. */
    for (size_t i = 0; i < rules->saves.walls; ++i) {
        game->wall_ids[i] = saved.wall_ids[i];
        game->wall_slots[i] = saved.wall_slots[i];
    }
    if (cok_ecl_var(vm, COK_VAR_3D) != 0) {
        /* The map reloads only if wall set 1's record is above 0. */
        if (saved.wall_ids[0] > 0 && !load_map(game, (uint8_t)cok_ecl_var(vm, COK_VAR_MAP)))
            return false;
        for (size_t i = 0; i < rules->saves.walls_restored; ++i) {
            if (saved.wall_ids[i] <= 0) continue;
            if (!load_walls(game, (uint8_t)saved.wall_slots[i], (uint8_t)saved.wall_ids[i]))
                return false;
        }
    } else {
        /* Outside 3D areas the overland map is loaded, not drawn. */
        cok_adventure_load_big(game, rules->view.overland_picture);
    }
    vm->last_mode = rules->saves.keep_last_mode ? saved.last_mode : saved.mode;
    vm->mode = 0; /* the party menu */
    cok_adventure_flush(game); /* 4b6d:20ef */
    return true;
}

bool cok_adventure_import_game(cok_adventure *game, const char *path)
{
    static cok_saved_game saved;
    cok_ecl *vm = &game->vm;
    const cok_rules *rules = cok_game_rules(game);
    const cok_rules_import *I = rules->characters.import;
    assert(I != NULL && "the game imports no other game's saved games");
    game->error[0] = '\0';
    if (!cok_saved_game_read(path, I->from, &saved, game->error, sizeof game->error)) return false;
    /* Of the variables, the other game's savings alone (46b2:1ef0 reads
     * Champions' word at 0x267 into 0x4c33 and 0x4c34). */
    const cok_rules_var *savings = &I->from->ecl.vars[COK_VAR_SAVINGS];
    size_t cell = savings->address - 0x4b00u;
    uint16_t value = saved.mem4b00[cell];
    if (savings->width > I->from->ecl.variable_bytes)
        value = (uint16_t)((value & 0xff) | (saved.mem4b00[cell + 1] & 0xff) << 8);
    cok_ecl_set_var(vm, COK_VAR_SAVINGS, value);
    vm->keep_vars = false; /* DS:8c57 */
    loaded_vars(game);
    char dir[4096];
    directory_of(path, dir, sizeof dir);
    const cok_character_layout *L = rules->character;
    for (size_t i = 0; i < saved.count && i < COK_PARTY_MAX; ++i) {
        char base[9], file[4200];
        cok_party_roster_name(saved.names[i], base);
        snprintf(file, sizeof file, "%s/%s.%s", dir, base, I->from->saves.record);
        FILE *exists = fopen(file, "rb");
        if (exists == NULL) continue; /* the original skips missing characters */
        fclose(exists);
        cok_character *character = malloc(sizeof *character);
        if (character == NULL) {
            fail(game, "out of memory");
            return false;
        }
        if (!cok_character_import(character, rules, dir, base, I->from->saves.record,
                                  &game->item_types, game->error, sizeof game->error)) {
            cok_character_free(character);
            free(character);
            return false;
        }
        cok_adventure_flush(game);
        /* 46b2:1ef0 frees an NPC of the other game; but the import does not
         * move Champions' NPC flag (+0xe7), so none is one here. */
        if (cok_field_u8(L, character->record, COK_FIELD_NPC) >= 0x80) {
            cok_character_free(character);
            free(character);
            continue;
        }
        if (!cok_party_add(&game->party, rules, character)) {
            cok_character_free(character);
            free(character);
            fail(game, "%s: the party is full", file);
            return false;
        }
        cok_ecl_add_var(vm, COK_VAR_PARTY_SIZE, 1);
    }
    if (!cok_arena_join(game, vm->file)) return false;
    vm->character = cok_party_record(&game->party, 0);
    vm->mode = 0;
    cok_adventure_flush(game); /* 46b2:26d4 */
    return true;
}

/* Enter the loaded block (2fd3:3b47): run its load vector, show the view if
 * it loaded files or stays in 3D, then run the after-move and location
 * vectors, starting over whenever NEWECL switches blocks, and keep the mode
 * as the last (DS:4b4a = 4b49, at 3b47's end only). first: the first pass
 * runs at the stack depth of a vector 2fd3:3c28 runs itself (eclplay's
 * block, cok_adventure_enter_block), else 6 below, through 3b47. */
static cok_ecl_status enter_block(cok_adventure *game, bool first)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_status status = COK_ECL_OK;
    bool restoring = game->restoring;
    uint16_t sp = game->battle_sp;
    game->restoring = true;
    do {
        game->battle_sp = first ? COK_BATTLE_SP : COK_BATTLE_SP - 6;
        first = false;
        free_frames(game);
        vm->reload = false;
        vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
        cok_ecl_set_var(vm, COK_VAR_EDGE, 0);
        vm->saved_character = vm->character; /* DS:43bf */
        status = cok_ecl_run(vm, vm->vectors[4]);
        if (status != COK_ECL_OK || vm->abort || vm->reload) continue;
        cok_ecl_set_var(vm, COK_VAR_BLOCK, vm->block);
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
    game->restoring = restoring;
    game->battle_sp = sp;
    vm->last_mode = vm->mode;
    return status;
}

/* As 2fd3:3c28 starts, up to loading the block (3775:0361, 3775:01e8). */
static cok_ecl_status start_block(cok_adventure *game, uint8_t block)
{
    cok_ecl *vm = &game->vm;
    game->redraw = true;
    game->files_loaded = game->pieces_loaded = game->map_loaded = false;
    game->frame_pending = cok_ecl_var(vm, COK_VAR_BLOCK) != 0;
    vm->saved_character = vm->character;
    vm->reload = false;
    vm->mode = 4;
    if (cok_ecl_var(vm, COK_VAR_BLOCK) == 0) cok_adventure_party(game); /* a new game's block */
    if (cok_ecl_var(vm, COK_VAR_3D) == 0) vm->mode = 3;
    return cok_adventure_load(game, block);
}

cok_ecl_status cok_adventure_enter(cok_adventure *game, uint8_t block)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_status status = start_block(game, block);
    if (status != COK_ECL_OK) return status;
    /* Only the load vector (3cc7); the after-move and location vectors
     * wait for the first command, and the last mode stays as it was. */
    status = cok_ecl_run(vm, vm->vectors[4]);
    if (status != COK_ECL_OK || vm->abort) return status;
    if (vm->reload) return enter_block(game, false);
    cok_ecl_set_var(vm, COK_VAR_BLOCK, vm->block);
    return COK_ECL_OK;
}

cok_ecl_status cok_adventure_enter_block(cok_adventure *game, uint8_t block)
{
    cok_ecl_status status = start_block(game, block);
    return status == COK_ECL_OK ? enter_block(game, true) : status;
}

/* The game clock. */

/* Advance moon i's days in its phase (0x4cfc-0x4cfe), and once they reach
 * its days (the game's moons), its phase (0x4cf9-0x4cfb), redrawing it on
 * the frame (57e4:001e). */
static void moon(cok_adventure *game, unsigned i)
{
    const cok_rules_moons *moons = &cok_game_rules(game)->moons;
    uint16_t days = moons->days[i];
    cok_ecl *vm = &game->vm;
    uint16_t in_phase = cok_ecl_var_at(vm, COK_VAR_MOON_DAYS, i);
    if (in_phase < days) {
        cok_ecl_set_var_at(vm, COK_VAR_MOON_DAYS, i, (uint16_t)(in_phase + 1));
        return;
    }
    uint16_t phase = cok_ecl_var_at(vm, COK_VAR_MOONS, i);
    cok_ecl_set_var_at(vm, COK_VAR_MOONS, i, (uint16_t)(phase < 3 ? phase + 1 : 0));
    uint16_t phase_now = cok_ecl_var_at(vm, COK_VAR_MOONS, i);
    cok_view_tile(&game->screen, &game->view,
                  moons->tile_base + ((phase_now + moons->tile[i]) & 0xff), moons->column[i], 0,
                  false);
    cok_ecl_set_var_at(vm, COK_VAR_MOON_DAYS, i, 0);
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
            const cok_character_layout *L = cok_game_rules(game)->character;
            for (size_t k = 0; k < game->party.count; ++k) {
                uint8_t *c = game->party.members[k]->record;
                uint16_t age = cok_field_u16(L, c, COK_FIELD_AGE);
                ++age;
                cok_field_set16(L, c, COK_FIELD_AGE, age);
            }
            continue;
        }
        ++clock[i + 1];
        clock[i] -= cok_clock_units[i];
        if (i == 3) {
            moon(game, 0); /* 57e4:0143 */
            moon(game, 1);
            moon(game, 2);
        }
    }
}

bool cok_adventure_pass_time(cok_adventure *game, unsigned unit, unsigned count)
{
    if (unit > 6) return true;
    uint16_t clock[7];
    for (size_t i = 0; i < COK_CLOCK_UNITS; ++i)
        clock[i] = cok_ecl_var_at(&game->vm, COK_VAR_CLOCK, i);
    for (unsigned i = 0; i < count; ++i) {
        ++clock[unit];
        carry(game, clock);
    }
    for (size_t i = 0; i < COK_CLOCK_UNITS; ++i)
        cok_ecl_set_var_at(&game->vm, COK_VAR_CLOCK, i, clock[i]);
    if (cok_effects_pass_time(&game->effects, unit, count)) return true;
    effect_failed(game);
    return false;
}

/* The adventure loop. */

void cok_adventure_fail(cok_adventure *game, cok_ecl_status status, const char *format, ...)
{
    /* Callers pass game->error itself as an argument: format apart. */
    char text[sizeof game->error];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    memcpy(game->error, text, sizeof text);
    log_text(game, "error", game->error);
    game->vm.status = status;
    game->vm.abort = true;
}

void cok_adventure_notice(cok_adventure *game, const char *text, uint8_t fg)
{
    log_text(game, "print", text);
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    cok_text_string(&game->screen, &game->font, text, 0, 24, fg, 0);
    wait_ms(game, game->speed * 100u); /* 1521:0b4b */
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
}

void cok_adventure_sound(cok_adventure *game, cok_sound_call call, unsigned value)
{
    if (game->hooks.sound != NULL) game->hooks.sound(game, call, value, game->hooks.context);
}

void cok_adventure_overlay(cok_adventure *game)
{
    cok_adventure_sound(game, COK_SOUND_OVERLAY, 0);
    cok_adventure_sound(game, COK_SOUND_OVERLAY_END, 0);
}

void cok_adventure_quit(cok_adventure *game)
{
    cok_adventure_sound(game, COK_SOUND_QUIT, 0);
    if (game->debug) cok_debug_close(game); /* 1614:0051 */
    game->quit = true;
    game->vm.abort = true;
}

void cok_adventure_log(cok_adventure *game, const char *kind, const char *text)
{
    log_text(game, kind, text);
}

cok_keyboard cok_adventure_keyboard(cok_adventure *game)
{
    return keyboard(game);
}

void cok_adventure_wait(cok_adventure *game, unsigned ms)
{
    wait_ms(game, ms);
}

bool cok_adventure_key_pending(cok_adventure *game)
{
    /* 1614:03c2: KeyPressed, or with the mouse or the joystick on a button
     * of theirs down or the stick pushed. */
    bool pending =
        game->hooks.key_pending != NULL && game->hooks.key_pending(game, game->hooks.context);
    if (cok_adventure_devices_on(game) && cok_adventure_device_pending(game)) pending = true;
    return pending;
}

void cok_adventure_flush(cok_adventure *game)
{
    if (game->hooks.flush != NULL) game->hooks.flush(game, game->hooks.context);
}

void cok_adventure_carry(cok_adventure *game, uint16_t clock[7])
{
    carry(game, clock);
}

uint8_t *cok_adventure_pick(cok_adventure *game, const char *prompt, uint8_t *who, bool exit_item,
                           bool *ended)
{
    return pick_character(game, prompt, who, exit_item, true, ended);
}

void cok_adventure_free_picture(cok_adventure *game)
{
    free_frames(game);
}

void cok_adventure_load_picture(cok_adventure *game, uint8_t id)
{
    load_picture(game, id);
}

void cok_adventure_show_picture(cok_adventure *game)
{
    draw_frame(game, game->frame);
}

void cok_adventure_show_frame(cok_adventure *game, size_t frame)
{
    draw_frame(game, frame);
}

bool cok_adventure_menu_animates(const cok_adventure *game, uint32_t *ms)
{
    if (!game->menu_animates || game->frame_unset || game->frame_count < 2 ||
        game->frame >= game->frame_count)
        return false;
    /* 67b5:0502: the delay (DS:6d9c + 8 * frame) times 10 against GetTime's
     * hundredths. */
    uint32_t delay = game->delays[game->frame];
    *ms = delay > UINT32_MAX / 100 ? UINT32_MAX : delay * 100;
    /* A delay of 0, as every group of a sprite has, the original's loop
     * takes as up at once, redrawing as fast as it runs; a hundredth,
     * GetTime's unit, here. */
    if (*ms < 10) *ms = 10;
    return true;
}

void cok_adventure_menu_frame(cok_adventure *game)
{
    game->frame = game->frame + 1 < game->frame_count ? game->frame + 1 : 0;
    draw_frame(game, game->frame);
}

void cok_adventure_print(cok_adventure *game, const char *text, cok_text_window window,
                         uint8_t fg, bool clear)
{
    log_text(game, "print", text);
    cok_adventure_wrap(game, &game->vm.cursor, text, window, fg, clear, page);
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

/* Area (475c:0a75): turn the overhead map on or off (DS:6d84) and redraw
 * the view (69ea:0820), the status line unchanged; or, where the area
 * hides the square (0x4bfb), say "Not Here" in yellow, unless the game was
 * started with Helm (ParamStr(2) against DS:896a). */
static void area(cok_adventure *game)
{
    if (cok_ecl_var(&game->vm, COK_VAR_HIDDEN) != 0 && !game->helm) {
        cok_adventure_notice(game, "Not Here", 14);
        return;
    }
    game->overhead = !game->overhead;
    log_text(game, "area", game->overhead ? "on" : "off");
    draw_view(game);
}

/* Turn by eighths of a full turn and redraw the view (475c:09ec). */
static void turn(cok_adventure *game, unsigned by)
{
    cok_ecl *vm = &game->vm;
    /* Turning left or right first plays sound 10 (DS:1e5c, 475c:0c58,
     * 0ca6), not logged. */
    cok_adventure_sound(game, COK_SOUND_COMMAND, 10);
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
    cok_ecl_set_var(vm, COK_VAR_EDGE, 0);
    if (cok_view_passage(&game->view, vm->direction, vm->map_x, vm->map_y) == 0) return;
    int x = vm->map_x, y = vm->map_y;
    cok_view_step(vm->direction, &x, &y);
    if (x > 15 || x < 0) {
        vm->map_x = x > 15 ? 15 : 0;
        cok_ecl_set_var(vm, COK_VAR_EDGE, 1);
    }
    if (y > 15 || y < 0) {
        vm->map_y = y > 15 ? 15 : 0;
        cok_ecl_set_var(vm, COK_VAR_EDGE, 1);
    }
}

/* Take a command from the overland menu (475c:09ec outside 3D areas, with
 * 0x4cf7 set): "Move Encamp", where Move changes the menu to "Exit" and
 * any special key then ends it, after turning the party to face the way
 * of an arrow or keypad key (H I M Q P O K G, north to north-west).
 * Special keys pick no character here. Returns 'E' to camp, the special
 * key's scan code to travel (Ctrl-F8's, 0x65 'e', camps instead), or
 * -1 if input ended. */
static int overland_command(cok_adventure *game)
{
    static const char ways[8] = {'H', 'I', 'M', 'Q', 'P', 'O', 'K', 'G'};
    for (;;) {
        bool special;
        int key;
        if (!game->moving) {
            key = menu_read(game, "", "Move Encamp", &special);
            if (key < 0) return -1;
            if (special) continue;
            if (key == 'M') {
                cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 67b5:0c7b */
                game->moving = true;
            } else if (key == 'E') {
                game->selected = 1;
                return key;
            }
            continue;
        }
        key = menu_read(game, "", "Exit", &special);
        if (key < 0) return -1;
        if (!special) {
            if (key == 'E') game->moving = false;
            continue;
        }
        for (uint8_t dir = 0; dir < 8; ++dir)
            if (key == ways[dir]) game->vm.direction = dir;
        cok_adventure_status(game);
        return key;
    }
}

/* Take a command from the adventure menu (475c:09ec): in a 3D area, 0 for
 * a step forward, 'E' to camp, 'L' to look; outside one, as
 * overland_command; or -1 if input ended. In any other case the original
 * returns a byte of its stack that it never sets: 9, from 3775:01e8's loop
 * counter, after a block is entered, other values after a NEWECL in the
 * load vector or when its overlay was loaded. The loop then travels in
 * mode 3 with no key read, step after step, which no shipped script
 * reaches; the run stops. */
static int command(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_set_var(vm, COK_VAR_STAY, 0);
    int result = 0;
    if (vm->mode == 3 && cok_ecl_var(vm, COK_VAR_OVERLAND) != 0) {
        result = overland_command(game);
    } else if (vm->mode != 4) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "the adventure's command in mode %u%s is a byte left on the stack "
                           "(475c:09ec)", vm->mode,
                           vm->mode == 3 ? " without the overland menu (0x4cf7)" : "");
        return -1;
    }
    for (bool done = vm->mode != 4; !done;) {
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
            case 'A': area(game); break;
            case 'C':
                /* 4888:0a0d for one whose status is 0; the original reads
                 * through NULL with none selected. */
                if (vm->character == NULL) {
                    cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                       "Cast with no character selected (475c:0ade)");
                    return -1;
                }
                if (cok_field_u8(cok_game_rules(game)->character, vm->character,
                                 COK_FIELD_STATUS) == 0) {
                    game->selected = 1;
                    cok_magic_cast(game, COK_CAST_COMMANDS);
                }
                break;
            case 'V': {
                game->selected = 1;
                bool used;
                cok_sheet(game, COK_SHEET_STALE_UNKNOWN, &used);
                break;
            }
            case 'E':
                game->selected = 1;
                done = true;
                break;
            case 'S':
                cok_ecl_set_var(vm, COK_VAR_SEARCH, cok_ecl_var(vm, COK_VAR_SEARCH) ^ 1);
                cok_adventure_status(game);
                break;
            case 'L':
                cok_ecl_set_var(vm, COK_VAR_SEARCH, cok_ecl_var(vm, COK_VAR_SEARCH) | 2);
                cok_adventure_status(game);
                cok_adventure_pass_time(game, 2, 1);
                done = true;
                break;
            default: break;
            }
            if (vm->abort) return -1;
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
        if (game->vm.abort) return -1;
    }
    if (result < 0) return result;
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
    /* Sound 10 (DS:1e5c, 475c:081a) plays first, not logged. */
    cok_adventure_sound(game, COK_SOUND_COMMAND, 10);
    wait_ms(game, 50);
    int x = vm->map_x, y = vm->map_y;
    cok_view_step(vm->direction, &x, &y);
    vm->map_x = (int8_t)(x < 0 ? 15 : x > 15 ? 0 : x);
    vm->map_y = (int8_t)(y < 0 ? 15 : y > 15 ? 0 : y);
    vm->ahead = cok_view_wall(&game->view, vm->direction, vm->map_x, vm->map_y);
    for (size_t i = 0; i < 3; ++i) game->door_tries[i] = true;
    vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
    log_position(game);
    cok_adventure_pass_time(game, (cok_ecl_var(vm, COK_VAR_SEARCH) & 1) != 0 ? 2 : 1, 1);
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
    const cok_character_layout *L = cok_game_rules(game)->character;
    bool opened = false;
    for (size_t i = 0; i < game->party.count && !opened; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        uint8_t strength = cok_field_u8_at(L, c, COK_FIELD_SCORE, COK_SCORE_STRENGTH),
                extra = cok_field_u8(L, c, COK_FIELD_STRENGTH_EXCEPTIONAL);
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
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (size_t i = 0; i < game->party.count; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        if (cok_field_s8_at(L, c, COK_FIELD_CLASS_LEVEL, class) >= 1) return true;
        if (cok_field_s8_at(L, c, COK_FIELD_FORMER_LEVEL, class) > 0 &&
            cok_character_former_class(c, cok_game_rules(game)))
            return true;
    }
    return false;
}

/* Pick (475c:05b6): each member rolls 1-100 against its lock picking
 * (+0xdc) until one who is okay succeeds. It is not offered again until
 * the next step. */
static bool pick(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    bool opened = false;
    for (size_t i = 0; i < game->party.count && !opened; ++i) {
        const uint8_t *c = game->party.members[i]->record;
        uint8_t r = roll(game, 1, 100);
        opened = r <= cok_field_u8_at(L, c, COK_FIELD_THIEF_SKILL, 2) &&
                 cok_field_u8(L, c, COK_FIELD_STATUS) == 0;
    }
    game->door_tries[1] = false;
    if (opened) open_door(game);
    return opened;
}

/* The first member who has memorized spell, and where (475c:06c6,
 * 475c:0683): the 58 bytes from +0x1e. */
static uint8_t *memorized(cok_adventure *game, uint8_t spell)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *c = game->party.members[i]->record;
        for (size_t s = 0; s < cok_field_count(L, COK_FIELD_MEMORIZED); ++s)
            if (cok_field_u8_at(L, c, COK_FIELD_MEMORIZED, s) == spell)
                return c + cok_field_offset_at(L, COK_FIELD_MEMORIZED, s);
    }
    return NULL;
}

/* Knock (475c:0720): the first member who has memorized spell 0x1f casts
 * it, and the party passes the door once; the door stays locked. */
static bool knock(cok_adventure *game)
{
    uint8_t *spell = memorized(game, cok_game_spell(game, COK_SPELL_KNOCK));
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
    if (game->door_tries[2] &&
        memorized(game, cok_game_spell(game, COK_SPELL_KNOCK)) != NULL)
        append(items, sizeof items, " Knock");
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

/* The overland map. */

/* The screen cell of the party's mark on the overland map: 127f:0edb and
 * 10e7 take 0x4bc3 + 1 and 0x4bc4 + 1 as words, the column times 4 bytes
 * and the row times 8 rows, unclipped. False where that lies off the
 * screen: the original reads and writes outside the row it starts on, or
 * beyond its table of rows. */
static bool mark_cell(cok_adventure *game, int *x, int *y)
{
    uint16_t across = cok_ecl_var(&game->vm, COK_VAR_OVERLAND_X),
             down = cok_ecl_var(&game->vm, COK_VAR_OVERLAND_Y);
    uint16_t column = (uint16_t)((uint16_t)(across + 1u) << 2);
    int16_t row = (int16_t)(uint16_t)((uint16_t)(down + 1u) << 3);
    if (column > 39 * 4 || row < 0 || row > 24 * 8) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "the party's mark on the overland map at %d,%d lies off the screen "
                           "(4877:0005)", (int16_t)across, (int16_t)down);
        return false;
    }
    *x = column / 4;
    *y = row / 8;
    return true;
}

/* Copy the screen cell at x, y to or from game->under (127f:0edb, 10e7). */
static void copy_cell(cok_adventure *game, int x, int y, bool save)
{
    size_t stride = (size_t)game->screen.units * 4;
    for (size_t row = 0; row < 8; ++row) {
        uint8_t *screen = game->screen.pixels + ((size_t)y * 8 + row) * stride + (size_t)x * 4;
        uint8_t *under = game->under.pixels + row * 4;
        if (save)
            memcpy(under, screen, 4);
        else
            memcpy(screen, under, 4);
    }
}

static bool have_under(cok_adventure *game)
{
    if (game->under.pixels != NULL) return true;
    if (cok_adventure_create_picture(game, &game->under, 1, 8, 1, 0) == COK_PICTURE_OK) return true;
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory");
    return false;
}

void cok_adventure_mark(cok_adventure *game)
{
    int x, y;
    if (!mark_cell(game, &x, &y) || !have_under(game)) return;
    copy_cell(game, x, y, true);
    /* The original draws the cell and the cursor, masked, into a picture
     * of a cell (DS:6166) and puts that on the screen; the dirty tables it
     * then clears (DS:4b90, 4c38, 4d88, 4ed8) are not ported. */
    cok_picture_draw(&game->screen, &game->cursor, 0, x, y, COK_DRAW_MASKED, NULL);
}

/* Put back the cell the mark covered, at the party's place now
 * (4877:00d6). */
static bool unmark(cok_adventure *game)
{
    int x, y;
    if (!mark_cell(game, &x, &y) || !have_under(game)) return false;
    copy_cell(game, x, y, false);
    return true;
}

void cok_adventure_travel(cok_adventure *game)
{
    const cok_data_segment *D = cok_game_rules(game)->data;
    cok_ecl *vm = &game->vm;
    if (!unmark(game)) return;
    cok_ecl_set_var(vm, COK_VAR_LAST_X, cok_ecl_var(vm, COK_VAR_OVERLAND_X));
    cok_ecl_set_var(vm, COK_VAR_LAST_Y, cok_ecl_var(vm, COK_VAR_OVERLAND_Y));
    /* A step across (DS:1ed6) or down (DS:1edf) for facings 0-8; a facing
     * past 8 reads on into the combat terrain table. */
    uint16_t across = (uint16_t)cok_data_offset(D, COK_DATA_STEP_X, vm->direction),
             down = (uint16_t)cok_data_offset(D, COK_DATA_STEP_Y, vm->direction);
    int8_t x = (int8_t)(uint8_t)(cok_ecl_var(vm, COK_VAR_OVERLAND_X) +
                                 (uint16_t)cok_cast_step_byte(D, across));
    int8_t y = (int8_t)(uint8_t)(cok_ecl_var(vm, COK_VAR_OVERLAND_Y) +
                                 (uint16_t)cok_cast_step_byte(D, down));
    x = x < 0 ? 0 : x > 0x25 ? 0x25 : x;
    y = y < 0 ? 0 : y > 0x0e ? 0x0e : y;
    cok_ecl_set_var(vm, COK_VAR_OVERLAND_X, (uint16_t)x);
    cok_ecl_set_var(vm, COK_VAR_OVERLAND_Y, (uint16_t)y);
    char text[32];
    snprintf(text, sizeof text, "%d,%d,%u", x, y, vm->direction);
    log_text(game, "overland", text);
    cok_adventure_mark(game);
    if (vm->abort) return;
    cok_adventure_pass_time(game, 3, 12);
}

/* Take the step chosen, unless the after-move vector set 0x7ec9 to 0xff
 * (475c:0e77): in a 3D area a square of its map, outside one a square of
 * the overland map, where 0x7ec9 stays as it is. */
static void step(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (vm->mode == 3) {
        if (cok_ecl_var(vm, COK_VAR_STAY) < 0xff) {
            cok_adventure_travel(game);
            cok_adventure_status(game);
        }
    } else if (vm->mode == 4 && cok_ecl_var(vm, COK_VAR_STAY) < 0xff) {
        game->redraw = true;
        uint8_t passage = cok_view_passage(&game->view, vm->direction, vm->map_x, vm->map_y);
        bool moved = passage == 1;
        if (passage == 2 || passage == 3) moved = locked_door(game, passage);
        if (moved) advance(game);
        cok_adventure_status(game);
    } else {
        cok_ecl_set_var(vm, COK_VAR_STAY, 0);
    }
    free_frames(game); /* 6961:0537 */
    cok_adventure_forget_portrait(game);
}

/* Camp (2fd3:3403): run the camp vector, then the camp menu (4888:2c31);
 * if an encounter interrupted a rest, redraw the screen and run the rest
 * vector. Then show the view, and clear DS:5885 (2fd3:344d), so that the
 * game counts as saved only in the camp it was saved in. A vector that
 * runs NEWECL enters the new block after the next command, as the
 * original does; one that ends the run stops here, where the original
 * still opens the camp menu. Outside 3D areas it then marks the party on
 * the overland map (4877:0005). */
static void redraw_screen(cok_adventure *game);

static cok_ecl_status camp(cok_adventure *game, uint16_t sp)
{
    cok_ecl *vm = &game->vm;
    uint16_t outer = game->battle_sp;
    game->battle_sp = sp;
    cok_ecl_status status = cok_ecl_run(vm, vm->vectors[2]);
    game->battle_sp = outer;
    if (status != COK_ECL_OK || vm->abort) return status;
    bool interrupted = cok_camp(game);
    if (vm->abort) return vm->status;
    if (interrupted) {
        redraw_screen(game);
        game->battle_sp = sp;
        status = cok_ecl_run(vm, vm->vectors[3]);
        game->battle_sp = outer;
        if (status != COK_ECL_OK || vm->abort) return status;
    }
    game->redraw = true;
    if (!no_view(vm)) cok_adventure_view(game);
    game->effects.rolls.saved = 0; /* DS:5885 */
    if (cok_ecl_var(vm, COK_VAR_3D) == 0 && !cok_ecl_flag(vm, COK_VAR_OFF_MAP))
        cok_adventure_mark(game);
    return vm->abort ? vm->status : COK_ECL_OK;
}

/* Look (2fd3:3c28): run the location vector once as if searching. */
static cok_ecl_status look(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t searching = cok_ecl_var(vm, COK_VAR_SEARCH) & 1;
    cok_ecl_set_var(vm, COK_VAR_SEARCH, 1);
    game->redraw = true;
    cok_adventure_view(game);
    cok_ecl_status status = cok_ecl_run(vm, vm->vectors[1]);
    if (status == COK_ECL_OK && !vm->abort && vm->reload) status = enter_block(game, false);
    cok_ecl_set_var(vm, COK_VAR_SEARCH, searching);
    return status;
}

/* Redraw the screen for the mode (6346:2c17): in a 3D area the frame, the
 * view unless 0x4c38 is set, the party list and the status line; in camp
 * the frame, the party list and the status line, loading the camp picture
 * (PIC record 0x3b), which the camp's menus show; for treasure (mode 6)
 * the same with the treasure's picture (PIC record 0x3c) and no status
 * line; in shops and the temple (mode 1) the frame but for their first
 * redraw (DS:883c), the small picture's first frame at cell 3, 3 (the
 * slot's first entry, DS:6da8, 6961:000a), or in 3D areas the portrait
 * last shown (DS:4b55, 4b56: 6961:05b9, 06bd), then the party list and
 * the status line; in the party menu (mode 0) the frame of 1128:0000,
 * cleared; in combat (mode 5) nothing. */
static void redraw_screen(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    game->redraw = true;
    if (vm->mode == 0) {
        cok_adventure_screen(game, COK_SCREEN_OPEN);
    } else if (vm->mode == 1) {
        if (game->shop_frame) cok_adventure_frame(game);
        if (cok_ecl_var(vm, COK_VAR_3D) == 0)
            draw_frame(game, 0);
        else
            show_portrait(game, game->portrait_head, game->portrait_body);
        if (vm->abort) return;
        cok_adventure_party(game);
        cok_adventure_status(game);
    } else if (vm->mode == 2 || vm->mode == 6) {
        uint8_t picture = vm->mode == 2 ? 0x3b : 0x3c;
        cok_adventure_frame(game);
        load_picture(game, picture);
        cok_adventure_party(game);
        if (vm->mode == 2) cok_adventure_status(game);
    } else if (vm->mode == 4) {
        cok_adventure_frame(game);
        if (!no_view(vm)) cok_adventure_view(game);
        cok_adventure_party(game);
        cok_adventure_status(game);
        game->frame_pending = false;
    } else if (vm->mode == 3 && game->picture_id != 9 && !cok_ecl_flag(vm, COK_VAR_OFF_MAP)) {
        cok_adventure_view(game);
    }
}

void cok_adventure_redraw(cok_adventure *game)
{
    redraw_screen(game);
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
    vm->status = COK_ECL_OK;
    /* A block entered from a saved game redraws the whole screen. */
    if (vm->mode != 3 && vm->keep_vars) {
        if (game->frame_pending) redraw_screen(game);
        game->redraw = true;
        cok_adventure_view(game);
    }
    vm->keep_vars = false;
    game->moving = false;
    /* A start menu that ended the game (dkk DS:8c5b) ends the loop, which
     * its caller starts again (3632:2e06). */
    while (status == COK_ECL_OK && !vm->abort && !game->new_game) {
        int key = next_command(game);
        if (key < 0) break;
        if (!vm->reload) cok_ecl_set_var(vm, COK_VAR_BLOCK, vm->block);
        /* The loop camps on UpCase(key) 'E', which Ctrl-F8 in the
         * overland's Move mode is too (scan code 0x65, 'e'). */
        while (status == COK_ECL_OK && !vm->abort &&
               (cok_ecl_var(vm, COK_VAR_SEARCH) > 1 || key == 'E' || key == 'e')) {
            status = key == 'E' || key == 'e' ? camp(game, COK_BATTLE_SP - 8) : look(game);
            if (status == COK_ECL_OK && !vm->abort) key = next_command(game);
        }
        if (status != COK_ECL_OK || vm->abort) break;
        status = cok_ecl_run(vm, vm->vectors[0]);
        if (status != COK_ECL_OK || vm->abort || game->new_game) break;
        if (vm->reload) {
            status = enter_block(game, false);
            continue;
        }
        cok_ecl_set_var(vm, COK_VAR_LAST_X, (uint16_t)vm->map_x);
        cok_ecl_set_var(vm, COK_VAR_LAST_Y, (uint16_t)vm->map_y);
        step(game);
        if (vm->status == COK_ECL_EFFECT_FAILED) break; /* the effect timers */
        cok_adventure_view(game);
        if (vm->abort) break;
        /* Sound 10 again (2fd3:3eb9, not logged) if the party's square
         * changed or is negative. */
        if (vm->map_x < 0 || vm->map_x != (int16_t)cok_ecl_var(vm, COK_VAR_LAST_X) ||
            vm->map_y < 0 || vm->map_y != (int16_t)cok_ecl_var(vm, COK_VAR_LAST_Y))
            cok_adventure_sound(game, COK_SOUND_COMMAND, 10);
        game->picture_shown = false;
        game->view_replaced = true;
        status = cok_ecl_run(vm, vm->vectors[1]);
        if (status == COK_ECL_OK && !vm->abort && vm->reload) status = enter_block(game, false);
    }
    /* The effect timers (cok_adventure_pass_time), the commands and the
     * view fail between runs. */
    if (status == COK_ECL_OK && vm->status != COK_ECL_OK) status = vm->status;
    vm->abort = false;
    return status;
}
