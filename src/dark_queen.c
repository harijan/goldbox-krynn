#define _POSIX_C_SOURCE 200809L /* opendir, strcasecmp */
#include "dark_queen.h"

#include "adventure.h"
#include "data_dark_queen.h"
#include "hlib.h"
#include "magic.h"
#include "monster.h"
#include "queen.h"
#include "queen_levels.h"
#include "queen_arena.h"
#include "queen_create.h"
#include "queen_items.h"
#include "queen_view.h"
#include "queen_train.h"
#include "round.h"
#include "queen_modify.h"
#include "shop.h"
#include "treasure.h"
#include "vga.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The libraries the game draws from, read once and kept. */
enum {
    LIB_ALWAYS, LIB_FRAME, LIB_TITLE, LIB_BACK, LIB_WALLS_B, LIB_WALLS_C, LIB_PIC_A, LIB_PIC_B,
    LIB_PIC_C, LIB_BIGPIC, LIB_GEN, LIB_FINAL, LIB_DUNGCOM, LIB_CBODY, LIB_WILDCOM, LIB_SPRIT,
    LIB_CPIC, LIB_COMSPR,
    LIBS
};
static const char *const lib_names[LIBS] = {
    "ALWAYS.TLB", "FRAME.TLB", "TITLE.TLB", "BACK.TLB",   "8X8DB.TLB", "8X8DC.TLB",
    "PICA.TLB",   "PICB.TLB",  "PICC.TLB",  "BIGPIC.TLB", "GEN.TLB",   "FINAL.TLB",
    "DUNGCOM.TLB", "CBODY.TLB",  "WILDCOM.TLB", "SPRIT.TLB", "CPIC.TLB", "COMSPR.TLB",
};
_Static_assert((int)LIB_DUNGCOM == (int)QUEEN_LIB_DUNGCOM && (int)LIB_CBODY == (int)QUEEN_LIB_CBODY &&
               (int)LIB_WILDCOM == (int)QUEEN_LIB_WILDCOM,
               "queen.h numbers the libraries");

enum {
    ICON_SLOTS = 0x26,       /* the combat icons' slots (DS:b17d, 0x26 a slot's two images) */
    FONT_SIZE = 96 * 12,     /* GAME.FON: 0x20-0x7f, 12 rows each (dqk 25a7:04b5) */
    WINDOW = 0x17,           /* the window's grey, colour 8 through DS:9d86 */
    WALL_SETS = 3,
    WALL_COLOURS = 37,       /* each slot's colours from 0x20 + 37 * slot (8a8b:119d) */
    VIEW_AT = 24,            /* the view's box, cells 3-13 (8a8b:085a) */
    VIEW_END = 112,
    BAR_ROW = 188,           /* the bottom bar's buttons, 0x1f9e (1bdb:0801) */
    NO_BIG = 0xff,           /* DS:ae43 with no big picture loaded */
    CONTROLS = 48,           /* the buttons of a screen the mouse's cursor knows */
    AREA_MENU = 0x100,       /* bar_menu_areas: a click on an area's index + this */
    BAR_ROW_TOP = 178,       /* the combat menu's two rows (1595's 0x1f99, 0x1f9f) */
    BAR_ROW_BOTTOM = 190,
};

/* The keys as the game reads them (251e:00e7, its table 251e:0114): the
 * ASCII byte, or an extended key's code from 0x100. */
enum {
    KEY_PAD5 = 0x100, KEY_PGUP = 0x101, KEY_RIGHT = 0x102, KEY_PGDN = 0x103, KEY_DOWN = 0x104,
    KEY_END = 0x105, KEY_LEFT = 0x106, KEY_HOME = 0x107, KEY_UP = 0x108, KEY_INS = 0x109,
    KEY_DEL = 0x10a, KEY_F1 = 0x110, KEY_ALT = 0xff, /* Alt and a letter: 0xff + the letter */
};

/* The graphics' state (DS:5eac, 5e98:0200): what the screen shows, whose
 * leaving frees what it held. */
enum { STATE_PICTURE = 0, STATE_BIG = 3, STATE_VIEW = 4, STATE_TITLE = 5, STATE_CAMP = 6 };

/* A rectangle of the screen, rows top to bottom and columns left to right,
 * the ends excluded. */
typedef queen_rect rect;

typedef struct {
    uint8_t *bytes;
    size_t size;
    hlib_library library;
    bool loaded;
} queen_lib;

/* A combat icon's picture record (19a4:0881's copy, header included); NULL bytes for empty. */
typedef struct {
    uint8_t *bytes;
    size_t size;
} queen_icon_record;

typedef struct {
    cok_vga_palette palette;
    uint8_t font[FONT_SIZE];
    queen_lib libs[LIBS];
    /* The wall sets of the 3D view's slots (DS:c393 + 4 * slot): their
     * library, loaded or not, and the colour each pixel takes. */
    hlib_library walls[WALL_SETS];
    bool wall_loaded[WALL_SETS];
    uint8_t remap[WALL_SETS][256];
    /* The small picture (DS:c2f0): its library, its frames (DS:b20c,
     * b20d), the one shown (b20e). */
    hlib_library picture;
    bool picture_loaded;
    uint8_t first_frame, last_frame, frame;
    /* The encounter's sprite (SPRIT.TLB's record of the sprite id): its palette
     * (record 0) and its groups, records 1-3, drawn over the view. */
    hlib_library sprite;
    /* The big picture (DS:b7a8): BIGPIC's library of the one loaded
     * (DS:ae43, NO_BIG for none) and the last asked for (DS:acd2). */
    hlib_library big;
    unsigned big_id, big_last;
    unsigned state; /* DS:5eac */
    /* The timer's time (DS:cda6, 72.8 Hz) as the game's waits pass it, in
     * milliseconds: the colours' cycles step by it. */
    uint64_t clock_ms;
    /* A menu's wait animating the small picture (1bdb:1008 with its anim
     * argument), the time its frame was last advanced, in milliseconds of
     * the clock; and the time animates last gave the front end. */
    bool menu_picture;
    uint64_t frame_from;
    uint32_t animate_ms;
    /* The controls on screen, for the mouse's cursor (2aa6:15e3: the sword
     * over one, the shield elsewhere). */
    rect controls[CONTROLS];
    /* Each control's cursor (ALWAYS.TLB's record, 2aa6:15e3's value; 0 for
     * the sword), the view's areas' arrows and U-turn. */
    uint8_t control_cursor[CONTROLS];
    size_t control_count;
    bool cursor_shown; /* 22b7:2122 shows it, 10d7:0141 hides it */
    bool mouse_started;
    /* The game saved since it last changed (DS:ae5b); a game loaded and
     * not yet entered (DS:b1ed); a big picture shown outside the
     * overland, which the party's list is not drawn over (DS:b256). */
    bool saved, loaded_game, big_no_party;
    /* DS:b7a7: the run is left while it is set (the adventure's loop tests
     * it, 5f07:126c-132e; 5f07:0fc4 halts the ECL run). Set by the camp when
     * the party is emptied (7368:06dc) and by a load asked (7070:2f2b for
     * Encamp, 7368:0761 for PROGRAM 0's camp); 6c70:2536 and 74b9:16ce set it
     * too, not ported. Cleared at the adventure's end (5f07:1338). Read by
     * 5f07:0940, not ported. */
    bool b7a7;
    /* DS:ae65: the text's delay (1a62:0476), which 0311 reads while it is set. It starts set
     * (6478:0725, and a new game's 6478:0916); PRINT, PRINTCLEAR and JOURNAL set it around their
     * text (6c70:1d02, 1d84, 32fc, 3323); the battle's setup clears it (59c7:1de2); 14eb sets it
     * again. The battle's exit sets it (5045:0781), not reached: the battle's end is a cited stop. */
    bool text_delay;
    /* The end of combat's flags (75cc:135c, 0593): DS:af3f, a member standing (0593 calls 01fb,
     * the experience, when it is set); DS:c7de, a member fled; DS:c7df, monsters present (11b0
     * sets it, 135c clears it); DS:b1da, the members out. */
    bool standing, fled, monsters_present;
    uint8_t out;
    /* DS:a260: the timers' flags, by the record's list index (1-based; 0x35 bytes, 1396:019f):
     * set outside camp by the memset, and in camp left by the last pass. */
    uint8_t timed[0x35];
    /* DS:ae4f: the share of the experience (01fb's, 414-41c), which 0427 gives out. */
    uint32_t share;
    /* DS:acc4: a load asked from the adventure's camp (5f07:0004 sets it to 1;
     * 5f07:000f reads it at 5f07:1372). The loop loads it (5f07:137a), then
     * main takes the camp from main again (5ebd:0312). */
    bool load_asked;
    /* The small picture's id (DS:ae42, 0xff for none: 63ae:02b0); DS:bd92,
     * which turns Train and Knight new Order on in the camp (0 at startup,
     * its stores found keep what it held); the overland's cell under the
     * party's mark (19a4:09eb's save, 0aba's restore: 8 by 8 pixels at
     * mark_x, mark_y), saved or not. */
    uint8_t picture_id;
    bool train_anywhere;
    uint8_t under[64];
    int mark_x, mark_y;
    bool marked;
    /* The battlefield's tile set (DUNGCOM's or WILDCOM's nested library, the
     * combat ground's; queen_arena.c), set by arena_tiles. */
    hlib_library arena;
    /* The combat icons (DS:b17d): image 0 (ready) and 1 (attacking) of each slot, the slot
     * + 0x26 * image of the original's table; the monsters' (CPIC), the party's (CBODY, or
     * CPIC for an NPC) and the missiles' and cursor's (COMSPR). Loaded by icon_load and
     * icon_pair; the battle's end frees 0x0f-0x25 in the original (5045:0001), not here. */
    queen_icon_record icons[2][ICON_SLOTS];
} queen;

static queen *own(cok_adventure *game)
{
    return game->own;
}

static void fail(cok_adventure *game, const char *format, const char *what, const char *why)
{
    snprintf(game->error, sizeof game->error, format, what, why);
}

static const hlib_library *lib(cok_adventure *game, unsigned which)
{
    queen *q = own(game);
    queen_lib *l = &q->libs[which];
    if (l->loaded) return &l->library;
    char path[sizeof game->assets + 64];
    cok_adventure_archive_path(game, lib_names[which], path, sizeof path);
    hlib_status status = hlib_read_file(path, &l->bytes, &l->size);
    if (status == HLIB_OK) status = hlib_parse(l->bytes, l->size, &l->library);
    if (status != HLIB_OK) {
        free(l->bytes);
        l->bytes = NULL;
        fail(game, "%.400s: %s", path, hlib_status_string(status));
        return NULL;
    }
    l->loaded = true;
    return &l->library;
}

/* A nested library, record id of library which. */
static bool sub_library(cok_adventure *game, unsigned which, unsigned id, hlib_library *out)
{
    const hlib_library *l = lib(game, which);
    const uint8_t *record;
    size_t size;
    if (l == NULL) return false;
    if (hlib_record(l, id, &record, &size) != HLIB_OK || hlib_parse(record, size, out) != HLIB_OK) {
        char what[32];
        snprintf(what, sizeof what, "%u", id);
        fail(game, "%s has no library %s", lib_names[which], what);
        return false;
    }
    return true;
}

/* The combat icons (DS:b17d, 19a4:0881). A record of library which, copied (header included;
 * an empty record, NULL). A record that is not there stops, citing 19a4:004b (the original's
 * disk loader fails there too; untraced, DARK-QUEEN.md's icons scope Q1). */
static bool icon_copy(cok_adventure *game, unsigned which, unsigned record, uint8_t **copy,
                      size_t *size)
{
    const hlib_library *l = lib(game, which);
    const uint8_t *data;
    *copy = NULL;
    *size = 0;
    if (l == NULL) return false;
    if (hlib_record(l, record, &data, size) != HLIB_OK) {
        snprintf(game->error, sizeof game->error, "%s has no record %u (19a4:004b)",
                 lib_names[which], record);
        return false;
    }
    if (*size == 0) return true;
    *copy = malloc(*size);
    if (*copy == NULL) {
        snprintf(game->error, sizeof game->error, "out of memory");
        return false;
    }
    memcpy(*copy, data, *size);
    return true;
}

/* 19a4:0881 for a pair: record rec0 of library which into image 0 of slot and rec1 into image
 * 1 (the callback 004b, then 0881 with flag 1: the record by its id). Both are copied before
 * either is stored, so a missing record stores neither (004b fails at 0193 before 0881). */
static bool icon_pair(cok_adventure *game, unsigned which, unsigned rec0, unsigned rec1,
                      unsigned slot)
{
    queen *q = own(game);
    if (slot >= ICON_SLOTS) {
        snprintf(game->error, sizeof game->error, "icon slot %u is past the table (DS:b17d)",
                 slot);
        return false;
    }
    uint8_t *copy[2] = {NULL, NULL};
    size_t size[2] = {0, 0};
    bool ok = icon_copy(game, which, rec0, &copy[0], &size[0]) &&
              icon_copy(game, which, rec1, &copy[1], &size[1]);
    if (!ok) {
        free(copy[0]);
        free(copy[1]);
        return false;
    }
    for (unsigned image = 0; image < 2; ++image) {
        queen_icon_record *icon = &q->icons[image][slot];
        free(icon->bytes);
        icon->bytes = copy[image];
        icon->size = size[image];
    }
    return true;
}

/* 204c:004d: the slot and its image 1 emptied (19a4:0881 with no record: 0x26 + slot). */
static void icon_clear(cok_adventure *game, unsigned slot)
{
    queen *q = own(game);
    if (slot >= ICON_SLOTS) return;
    for (unsigned image = 0; image < 2; ++image) {
        free(q->icons[image][slot].bytes);
        q->icons[image][slot].bytes = NULL;
        q->icons[image][slot].size = 0;
    }
}

/* The loader 204c:00df for a library (CPIC or CBODY), record id into slot: image 0 is record
 * id, or 100 + id where id is missing (19a4:004b, 004b:0056-0087); image 1 is id + 0x80
 * (004b:0140-0191). A slot past the table stops (DS:b17d). Not ported: the head layer of a
 * "CH" name (204c:00e5-0157, 0232, 030f:03a2): the game has no CHEAD, so DS:a848 is 0 and it
 * is dead; the CBODY whole-library path (DS:a6b4, 01d8) is creation's only (6523, 65fa). */
static bool icon_load(cok_adventure *game, unsigned which, unsigned id, unsigned slot)
{
    const hlib_library *l = lib(game, which);
    const uint8_t *data;
    size_t size;
    if (l == NULL) return false;
    unsigned record = hlib_record(l, id, &data, &size) == HLIB_OK ? id : 100 + id;
    return icon_pair(game, which, record, id + 0x80, slot);
}

/* 604b:05d7: CBODY +0xc4 into slot +0xc5 (00df("CBODYS", +0xc4, +0xc5), with no NPC test). The
 * other callers of 05d7: creation's icon editor (66e5, which draws CBODY itself, queen_create.c)
 * and the camp's Alt Icon (7070:1ce2, not ported: whoever ports it calls this). */
static bool body_icon(cok_adventure *game, const uint8_t *r)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    return icon_load(game, LIB_CBODY, cok_field_u8(L, r, COK_FIELD_ICON_BODY),
                     cok_field_u8(L, r, COK_FIELD_ICON));
}

/* A party member's combat icon (604b:2ab4's loop, 2ade-2b26): CBODY by body_icon, or an NPC
 * (+0x9b >= 0x80, 2af4-2b09) CPIC +0xbd (604b:2886 sets it) into +0xc5. Used by loaded() and
 * the import, where the loop is. */
static bool member_icon(cok_adventure *game, const uint8_t *r)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (cok_field_u8(L, r, COK_FIELD_NPC) < 0x80) return body_icon(game, r);
    return icon_load(game, LIB_CPIC, cok_field_u8(L, r, COK_FIELD_MONSTER_ID),
                     cok_field_u8(L, r, COK_FIELD_ICON));
}

bool queen_added_icons(cok_adventure *game, const uint8_t *record)
{
    return body_icon(game, record);
}

void queen_remove_icons(cok_adventure *game, const uint8_t *record)
{
    icon_clear(game, cok_field_u8(cok_game_rules(game)->character, record, COK_FIELD_ICON));
}

bool queen_icon(const cok_adventure *game, unsigned slot, unsigned image, const uint8_t **record,
                size_t *size)
{
    const queen *q = game->own;
    if (q == NULL || slot >= ICON_SLOTS || image > 1) return false;
    *record = q->icons[image][slot].bytes;
    *size = q->icons[image][slot].size;
    return true;
}

/* Time and the colours' cycles. */

/* The timer's ticks (251e:004d: the PIT's divisor 0x4000, 72.8 Hz). */
static uint32_t ticks(const queen *q)
{
    return (uint32_t)(q->clock_ms * 1193182u / 16384000u);
}

/* Milliseconds of the clock until tick t. */
static uint64_t ms_until(const queen *q, uint32_t t)
{
    int32_t left = (int32_t)(t - ticks(q));
    if (left <= 0) return 0;
    uint64_t at = ((uint64_t)ticks(q) + (uint64_t)left) * 16384000u;
    return (at + 1193181u) / 1193182u - q->clock_ms;
}

/* The cycles' random(8) (261b:00b1 through 2102:0234): the port's game
 * generator. */
static unsigned cycle_random(void *context, unsigned range)
{
    cok_adventure *game = context;
    return cok_tp_random(&game->vm.seed, (uint16_t)range);
}

/* Let ms pass as 1df9:052a does, the cycles stepping as it does (05bb): in
 * steps to each cycle's time, so that a front end shows them. Without its
 * draw: the waits this does not port (print_window's 1df9:009e, timed_wait). */
static void wait_cycles(cok_adventure *game, unsigned ms)
{
    queen *q = own(game);
    for (;;) {
        uint64_t step = ms;
        uint32_t next;
        if (cok_vga_cycle_next(&q->palette, &next)) {
            uint64_t due = ms_until(q, next);
            if (due < step) step = due > 0 ? due : 1;
        }
        if (step > ms) step = ms;
        if (step > 0) cok_adventure_wait(game, (unsigned)step);
        q->clock_ms += step;
        ms -= (unsigned)step;
        cok_vga_cycle_colours(&q->palette, ticks(q));
        if (ms == 0 || game->vm.abort) break;
    }
}

/* Let ms pass (1df9:052a, the game's wait): for ms != 0 one random(1000) first, at 1df9:058a,
 * whose value only rounds the timer's ticks (ms * 0x48 / 1000, plus one when the draw is below
 * the remainder), and the wait (wait_cycles). Its callers are ports of it: message_at
 * (1a62:0960), animate (1a62:0949's 100 ms times var 0x12), step_ahead (6fea:00d4's 50 ms),
 * and the bar message of Guard (177e:153c, message_at). */
static void pass(cok_adventure *game, unsigned ms)
{
    if (ms != 0) cok_tp_random(&game->vm.seed, 1000);
    wait_cycles(game, ms);
}

void cok_dark_queen_wait(cok_adventure *game, unsigned ms)
{
    pass(game, ms);
}

/* A library's palette record id into the palette (2796:0536, 261b:00b1),
 * its colours and cycles moved shift entries on. */
static void set_palette(cok_adventure *game, const hlib_library *l, unsigned id, unsigned shift)
{
    const uint8_t *record;
    size_t size;
    if (hlib_record(l, id, &record, &size) != HLIB_OK || !cok_vga_is_palette(record, size)) return;
    queen *q = own(game);
    cok_random random = {cycle_random, game};
    cok_vga_apply_palette(&q->palette, record, size, shift, ticks(q), &random);
}

/* Colours first to 0xff black, flushed (261b:00b1 with zeros and no
 * cycles, then 261b:0455). */
static void black(cok_adventure *game, unsigned first)
{
    queen *q = own(game);
    cok_vga_set_colours(&q->palette, first, 0x100 - first, NULL, NULL, 0, ticks(q), NULL);
}

static void draw(cok_adventure *game, const hlib_library *l, unsigned id, int row, int col,
                 const cok_vga_clip *clip, const uint8_t *remap)
{
    if (l != NULL) cok_vga_draw_record(&game->screen, l, id, row, col, clip, remap);
}

static void draw_frame_record(cok_adventure *game, unsigned id, int row, int col,
                              const cok_vga_clip *clip)
{
    draw(game, lib(game, LIB_FRAME), id, row, col, clip, NULL);
}

/* Fill rows y0 to y1 and columns x0 to x1, the ends excluded (22b7:0078). */
static void fill(cok_adventure *game, int y0, int x0, int y1, int x1, uint8_t colour)
{
    cok_vga_fill(&game->screen, x0, y0, x1 - x0, y1 - y0, colour);
}

/* Cells x1-x2 by y1-y2, inclusive, cleared to the window's grey (1a62:09a1). */
static void clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2)
{
    fill(game, 8 * y1, 8 * x1, 8 * (y2 + 1), 8 * (x2 + 1), WINDOW);
}

/* Text. */

/* A colour nibble's VGA colour (DS:9d86, from DS:6d78 by 269b:011c): its
 * own but 8, the window's grey. */
static uint8_t colour(unsigned nibble)
{
    nibble &= 15;
    return nibble == 8 ? WINDOW : (uint8_t)nibble;
}

/* A string at pixel row, col in the attribute's colours (2102:03a0: bg in
 * the high nibble), 8 pixels a character. */
static void text(cok_adventure *game, int row, int col, const char *s, unsigned attribute)
{
    for (; *s != '\0'; ++s, col += 8)
        cok_vga_glyph(&game->screen, own(game)->font, row, col, *s, colour(attribute),
                      colour(attribute >> 4));
}

/* 1a62:0046 on row 24 (0x18): the text boxed in black from cell x + 1,
 * fg on bg. */
static void prompt_box(cok_adventure *game, int x, const char *s, unsigned fg, unsigned bg)
{
    int x0 = (x + 1) * 8, x1 = x0 + 8 * (int)strlen(s);
    fill(game, 187, x0 - 1, 188, x1 + 1, 0);
    fill(game, 188, x0, 189, x1, colour(bg));
    fill(game, 188, x0 - 1, 197, x0, 0);
    fill(game, 188, x1, 197, x1 + 1, 0);
    fill(game, 197, x0 - 1, 198, x1 + 1, 0);
    text(game, 189, x0, s, (bg & 15) << 4 | (fg & 15));
}

/* A button's flags (the control's +0x1c, 2aa6): disabled, pressed, and
 * its text white with no hotkey letter. */
enum { BUTTON_DISABLED = 0x01, BUTTON_PRESSED = 0x08, BUTTON_WHITE = 0x40 };

/* A framed button (1bdb:008e) of width characters at row, col: raised,
 * FRAME 13, 14 a character, 15 over the last; pressed or disabled, 16, 17
 * and 18. Its text a row down: raised in 0x87 (0x8f white), its first
 * character again in 0x8f, the hotkey, unless white; pressed in 0x83
 * (0x8b white), the hotkey in 0x8b unless white; disabled in 0x80, no
 * hotkey. */
static void button(cok_adventure *game, int row, int col, const char *s, int width,
                   unsigned flags)
{
    bool sunk = flags & (BUTTON_DISABLED | BUTTON_PRESSED);
    unsigned base = sunk ? 16 : 13;
    draw_frame_record(game, base, row, col, NULL);
    for (int i = 0; i < width; ++i) draw_frame_record(game, base + 1, row, col + 8 * i, NULL);
    draw_frame_record(game, base + 2, row, col + 8 * (width - 1), NULL);
    bool white = flags & BUTTON_WHITE;
    unsigned attribute = flags & BUTTON_DISABLED ? 0x80
                         : flags & BUTTON_PRESSED ? (white ? 0x8b : 0x83)
                                                  : (white ? 0x8f : 0x87);
    text(game, row + 1, col, s, attribute);
    if (!(flags & BUTTON_DISABLED) && !white && s[0] != '\0') {
        char first[2] = {s[0], '\0'};
        text(game, row + 1, col, first, flags & BUTTON_PRESSED ? 0x8b : 0x8f);
    }
}

/* A button's place for a click (1bdb:008e's message 2): rows from 2 above
 * it to 10 below its top, columns from 4 before it to 4 past its text. */
static rect button_rect(int row, int col, int width)
{
    return (rect){row - 2, col - 4, row + 10, col + 8 * width + 4};
}

static bool inside(const rect *r, int x, int y)
{
    return y >= r->top && y < r->bottom && x >= r->left && x < r->right;
}

/* 1bdb:036c: the bottom bar drawn again, FRAME 4 within rows 186-199. */
static void bottom_bar(cok_adventure *game)
{
    const cok_vga_clip clip = {186, 0, 200, 320};
    draw_frame_record(game, 4, 0, 0, &clip);
}

/* A pick's bar (1bdb:1997; adventure.h's cok_drawing): the bar drawn, the prompt
 * (with its blank, as the pick gives it) boxed as bar_menu_areas boxes one, then the
 * buttons Select, and Exit where exit_item, a character's gap apart. The keys are the
 * pick's, which read them. */
static void pick_bar(cok_adventure *game, const char *prompt, bool exit_item)
{
    bottom_bar(game);
    size_t n = strlen(prompt);
    if (n > 0) prompt_box(game, 0, prompt, 0, 7);
    int col = 8 + 8 * (int)n;
    static const char *const items[] = {"Select", "Exit"};
    for (size_t i = 0; i < (exit_item ? 2u : 1u); ++i) {
        int width = (int)strlen(items[i]);
        button(game, BAR_ROW, col, items[i], width, 0);
        col += (width + 1) * 8;
    }
}

/* Keys and the mouse. */

/* An extended key's scan code as 251e:00e7 codes it. */
static int extended(int scan)
{
    switch (scan) {
    case 0x48: return KEY_UP;
    case 0x49: return KEY_PGUP;
    case 0x4d: return KEY_RIGHT;
    case 0x51: return KEY_PGDN;
    case 0x50: return KEY_DOWN;
    case 0x4f: return KEY_END;
    case 0x4b: return KEY_LEFT;
    case 0x47: return KEY_HOME;
    case 0x4c: return KEY_PAD5;
    case 0x52: return KEY_INS;
    case 0x53: return KEY_DEL;
    default: break;
    }
    if (scan >= 0x3b && scan <= 0x44) return KEY_F1 + scan - 0x3b;
    /* Alt and a letter, by the keyboard's rows. */
    static const char *const rows[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    static const int starts[] = {0x10, 0x1e, 0x2c};
    for (size_t r = 0; r < 3; ++r)
        if (scan >= starts[r] && scan < starts[r] + (int)strlen(rows[r]))
            return KEY_ALT + rows[r][scan - starts[r]];
    return 0;
}

/* What a read gave: a key's code, or a click of either button at x, y in
 * pixels (2aa6:15e3 takes the left and the right alike). */
typedef queen_input input;

/* Startup's part for the mouse (22b7:1be3, from 2102:0038): on wherever a
 * driver answers (no file says otherwise), the pointer at pixel 8, 8
 * (DS:cb55, cb77). Once, before the first read or cursor. */
static void mouse_start(cok_adventure *game)
{
    queen *q = own(game);
    if (q->mouse_started || !game->mouse.driver.installed) return;
    q->mouse_started = true;
    game->mouse.on = true;
    cok_mouse_driver_set(&game->mouse.driver, 16, 8);
}

/* Read a key or a click (2aa6:15e3: 251e:00e7, 22b7:235f). -1 when the
 * keys ran out, COK_KEY_TIMEOUT when a timed menu's time passed. */
static int read_input(cok_adventure *game, input *in)
{
    mouse_start(game);
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key = keys.read(keys.context);
    *in = (input){0};
    if (key < 0) return key;
    if (game->pointer.clicked) {
        in->click = true;
        in->x = game->pointer.x;
        in->y = game->pointer.y;
        return 0;
    }
    if (key == 0) {
        int scan = keys.read(keys.context);
        if (scan < 0) return scan;
        key = extended(scan);
    }
    return in->key = key;
}

/* 2aa6:000e: whether hotkey h takes key: '@' Return (0x0d or 0x0a), '#'
 * Escape (or '`'), '*' any, below 0x20 the extended code h + 0x100, else
 * the letter in either case (10d7:25b1 lowers AL alone: an extended key
 * is no letter). */
static bool hotkey(int h, int key)
{
    if (h == 0) return false;
    if (h < 0x20) return key == h + 0x100;
    if (h == '#') return key == 0x1b || key == 0x60;
    if (h == '*') return true;
    if (h == '@') return key == 0x0d || key == 0x0a;
    if (key > 0xff) return false;
    int a = h >= 'A' && h <= 'Z' ? h + 0x20 : h, b = key >= 'A' && key <= 'Z' ? key + 0x20 : key;
    return a == b;
}

/* The controls the mouse's cursor knows (cleared with the screen's). */
static void controls_clear(cok_adventure *game)
{
    own(game)->control_count = 0;
}

/* A control with the cursor shown over it (ALWAYS.TLB's record; 0, the
 * sword, for control_add). */
static void control_add_cursor(cok_adventure *game, rect r, uint8_t cursor)
{
    queen *q = own(game);
    if (q->control_count < CONTROLS) {
        q->control_cursor[q->control_count] = cursor;
        q->controls[q->control_count++] = r;
    }
}

static void control_add(cok_adventure *game, rect r)
{
    control_add_cursor(game, r, 0);
}

/* A key nothing takes (2102:0193): the speaker's beep, not sounded yet
 * (DARK-QUEEN.md's step 5). */
static void beep(cok_adventure *game)
{
    (void)game;
}

/* Whether key is Return (the '@' of 2aa6:000e: 0x0d or 0x0a). */
static bool is_return(int key)
{
    return key == '\r' || key == '\n';
}

/* The small picture's frame DS:b20e at cell 3, 3 (63ae:000c: record 1,
 * then for a frame past 0 its record over it, the overlays being
 * smaller), its palette set (19a4:0b8c). */
static void picture_frame(cok_adventure *game)
{
    queen *q = own(game);
    draw(game, &q->picture, 1, VIEW_AT, VIEW_AT, NULL, NULL);
    if (q->frame != 0) draw(game, &q->picture, q->frame + 1u, VIEW_AT, VIEW_AT, NULL, NULL);
    set_palette(game, &q->picture, 0, 0);
}

/* The next frame: past the last the loop's first. */
static void next_frame(queen *q)
{
    q->frame = q->frame >= q->last_frame ? q->first_frame : (uint8_t)(q->frame + 1);
}

/* A menu's wait (1bdb:1008) with its anim argument: the small picture
 * animates in it where one is loaded with frames (DS:c2ef, b20d), drawn
 * at each pass of its loop, the first at once, the next frame after each
 * half second (its 50 hundredths, 1a62:0979). */
static void menu_wait_begin(cok_adventure *game, bool anim)
{
    queen *q = own(game);
    q->menu_picture = anim && q->picture_loaded && q->last_frame != 0;
    q->frame_from = q->clock_ms;
    if (q->menu_picture) picture_frame(game);
}

static void menu_wait_end(cok_adventure *game)
{
    own(game)->menu_picture = false;
}

/* 1bdb:0495: "Press [Return] to continue." and its one button, Return
 * (0x70: white, its hotkey '@'), a click on it or Return (or Ctrl-Enter's
 * 0x0a) taking it, which draws it pressed; other keys and clicks beep.
 * After it the bar is drawn again where the caller does (ECL's PRESS KEY,
 * 5f07:1383), not where paging asks (1a62:0311). False if the keys ran
 * out. */
static bool continue_prompt(cok_adventure *game, bool redraw, bool anim)
{
    bottom_bar(game);
    prompt_box(game, 7, "Press        to continue.", 0, 7);
    button(game, BAR_ROW, 112, "Return", 6, BUTTON_WHITE);
    controls_clear(game);
    rect r = button_rect(BAR_ROW, 112, 6);
    control_add(game, r);
    menu_wait_begin(game, anim);
    bool taken = false;
    for (;;) {
        input in;
        int key = read_input(game, &in);
        if (key < 0 && key != COK_KEY_TIMEOUT) break;
        if (in.click ? inside(&r, in.x, in.y) : is_return(in.key)) {
            taken = true;
            break;
        }
        beep(game);
    }
    menu_wait_end(game);
    controls_clear(game);
    if (!taken) return false;
    button(game, BAR_ROW, 112, "Return", 6, BUTTON_WHITE | BUTTON_PRESSED);
    if (redraw) bottom_bar(game);
    return true;
}

static bool delimiter(char c)
{
    return c != '\0' && strchr("-.,?!:;", c) != NULL; /* DS:1cf5 */
}

/* 1a62:0311: text word by word in the window of cells x1-x2 by y1-y2 from
 * the cursor (DS:ae4d, ae4e), fg on the window's grey, a character at a
 * time with the text speed's delay (3 ms times 0x12) while DS:ae65 (the
 * text_delay state) is set; clear
 * first clears it and homes the cursor. A word that does not fit goes to
 * the next row (one that fits but for its space is printed first);
 * past the last row "Press [Return] to continue." (its bar left as it is)
 * and the window cleared. False if the keys ran out. */
static bool print_window(cok_adventure *game, const char *s, int x1, int y1, int x2, int y2,
                         unsigned fg, bool clear)
{
    cok_text_cursor *c = &game->vm.cursor;
    const bool slow = own(game)->text_delay; /* 1a62:0476, 0517, 0561 */
    unsigned attribute = 8u << 4 | (fg & 15);
    if (c->x < x1 || c->x > x2 || c->y < y1 || c->y > y2) *c = (cok_text_cursor){x1, y1};
    if (clear) {
        clear_cells(game, x1, y1, x2, y2);
        *c = (cok_text_cursor){x1, y1};
    }
    int last = (int)strlen(s) - 1, di = 0;
    unsigned ms = 3u * cok_ecl_load_var(&game->vm, 0x12);
    while (di <= last) {
        int si = di;
        while (si < last && delimiter(s[si])) ++si;
        while (si < last && !delimiter(s[si]) && s[si] != ' ') ++si;
        if (s[si] != ' ')
            while (si + 1 < last && delimiter(s[si + 1])) ++si;
        if (si - di + c->x > x2) {
            if (si - di + c->x == x2 + 1 && s[si] == ' ') {
                --si;
                for (; di <= si; ++di, ++c->x) {
                    char one[2] = {s[di], '\0'};
                    text(game, 8 * c->y, 8 * c->x, one, attribute);
                    if (slow) pass(game, ms);
                }
            }
            c->x = x1;
            ++c->y;
            while (di <= last && s[di] == ' ') ++di;
            if (c->y > y2 && di < last) {
                *c = (cok_text_cursor){x1, y1};
                if (!continue_prompt(game, false, false)) return false;
                clear_cells(game, x1, y1, x2, y2);
            }
            continue;
        }
        for (; di <= si; ++di, ++c->x) {
            char one[2] = {s[di], '\0'};
            text(game, 8 * c->y, 8 * c->x, one, attribute);
            if (slow) pass(game, ms);
        }
    }
    if (c->x > x2) {
        c->x = x1;
        ++c->y;
    }
    return true;
}

/* The adventure's text window hooks (adventure.h's cok_drawing): ADD EP's words
 * (1a62:0311, with the text delay state) and WHO's fill (1a62:09a1). */
static void window_text(cok_adventure *game, const char *text, unsigned fg, bool clear)
{
    print_window(game, text, 1, 0x11, 0x26, 0x16, fg, clear);
}

static void window_clear(cok_adventure *game)
{
    clear_cells(game, 1, 0x11, 0x26, 0x16);
}

/* The ECL's text window, cells 1-38 by 17-22, in colour fg. */
static bool print_colour(cok_adventure *game, const char *s, bool clear, unsigned fg)
{
    cok_adventure_log(game, "print", s);
    return print_window(game, s, 1, 0x11, 0x26, 0x16, fg, clear);
}

/* PRINT's text: colour 7, or 3 where variable 0x39 is set (6c70:1cd3). */
static bool print_text(cok_adventure *game, const char *s, bool clear)
{
    unsigned fg = cok_ecl_load_var(&game->vm, 0x39) != 0 ? 3 : 7;
    return print_colour(game, s, clear, fg);
}

/* The bottom bar's menu (1bdb:1e29, its bar built by 0d26, 08ef and
 * 0801, read by 1206 and 1008): the prompt, if any, boxed from cell 1
 * (1a62:0046 in black on grey: ending in a blank or '@' after a ':' it
 * loses its last character; not ending in ':', a blank, or '@', it takes
 * a blank), then a button an item from 8 pixels past it, a character's
 * gap between them, each read by its first letter in either case or a
 * click (the first item with that letter), which draws it pressed. A key
 * filter before them (dirs: 1bdb:064f, the extended keys, Escape and '`',
 * Alt and a letter; else 06c6, Escape and '`' alone) gives the key's code
 * instead: an extended one as (k & 0x7f) + 0x80, '`' as Escape. Nothing
 * else is taken: no item is current, Return picks none (the keys beep).
 * timeout: hundredths of a second after which the menu gives timed_out
 * (DS:c5ce, c5d2), 0 for none; anim: the small picture animates as it
 * waits. Returns an item's index, a filtered key's code, -1 if the keys
 * ran out. */
static int bar_menu_areas(cok_adventure *game, const char *prompt, const char *const *items,
                          size_t count, bool dirs, bool anim, uint16_t timeout, int timed_out,
                          const rect *areas, const uint8_t *area_cursors, size_t area_count,
                          int split, bool space);

static int bar_menu(cok_adventure *game, const char *prompt, const char *const *items,
                    size_t count, bool dirs, bool anim, uint16_t timeout, int timed_out)
{
    return bar_menu_areas(game, prompt, items, count, dirs, anim, timeout, timed_out, NULL, NULL,
                          0, -1, false);
}

/* bar_menu with areas of the screen after its buttons (1bdb:1429's: the
 * sheet's rows, which a click takes: 2aa6:15e3's hit, 1bdb:1206's), a
 * click on one giving AREA_MENU + its index; area_cursors, if not NULL,
 * the cursor shown over each (ALWAYS.TLB's record; NULL: the sword). */
static int bar_menu_areas(cok_adventure *game, const char *prompt, const char *const *items,
                          size_t count, bool dirs, bool anim, uint16_t timeout, int timed_out,
                          const rect *areas, const uint8_t *area_cursors, size_t area_count,
                          int split, bool space)
{
    /* 1e3d-1e5a: the prompt DS:3aee (10d7:2543: equal strings) is no menu: 1bdb:0521 draws
     * 0495's continue prompt and waits for its Return (141e(2), 1206 with no items and anim),
     * and 1e29 gives 0x0d with DS:c2f5 clear. */
    if (strcmp(prompt, "Press button or enter to continue") == 0)
        return continue_prompt(game, false, anim) ? 0x0d : -1;
    char box[96];
    snprintf(box, sizeof box, "%s", prompt);
    size_t n = strlen(box);
    if (n >= 2 && (box[n - 1] == ' ' || box[n - 1] == '@') && box[n - 2] == ':')
        box[--n] = '\0';
    else if (n > 0 && box[n - 1] != ':' && box[n - 1] != ' ' && box[n - 1] != '@' &&
             n + 1 < sizeof box)
        box[n++] = ' ', box[n] = '\0';
    bottom_bar(game);
    if (n > 0) prompt_box(game, 0, box, 0, 7);
    enum { ITEMS = 20 }; /* DS:af15's pairs */
    if (count > ITEMS) count = ITEMS;
    int cols[ITEMS], rows[ITEMS];
    rect rects[ITEMS];
    int col = 8 + 8 * (int)n;
    controls_clear(game);
    for (size_t i = 0; i < count; ++i) {
        /* split >= 0 (1595's two rows, 0x1f99 and 0x1f9f): the items up to split on row 178,
         * the rest from column 8 on row 190; split < 0 one row on BAR_ROW. */
        if (split >= 0 && i == (size_t)split + 1) col = 8;
        int row = split < 0 ? BAR_ROW : (int)i <= split ? BAR_ROW_TOP : BAR_ROW_BOTTOM;
        int width = (int)strlen(items[i]);
        cols[i] = col;
        rows[i] = row;
        rects[i] = button_rect(row, col, width);
        control_add(game, rects[i]);
        button(game, row, col, items[i], width, 0);
        col += (width + 1) * 8;
    }
    for (size_t i = 0; i < area_count; ++i)
        control_add_cursor(game, areas[i], area_cursors != NULL ? area_cursors[i] : 0);
    game->timed = timeout != 0;
    game->timeout = timeout;
    menu_wait_begin(game, anim);
    int result = -1;
    for (;;) {
        input in;
        int key = read_input(game, &in);
        if (key == COK_KEY_TIMEOUT && timeout != 0) {
            result = timed_out;
            break;
        }
        if (key < 0 && key != COK_KEY_TIMEOUT) break;
        int picked = -1;
        if (in.click) {
            for (size_t i = 0; i < count && picked < 0; ++i)
                if (inside(&rects[i], in.x, in.y)) picked = (int)i;
            for (size_t i = 0; i < area_count && picked < 0; ++i)
                if (inside(&areas[i], in.x, in.y)) {
                    result = AREA_MENU + (int)i;
                    goto done;
                }
        } else {
            int k = in.key;
            if (k == KEY_ALT + 'S' || k == KEY_ALT + 'T') {
                /* 064f, 06c6: the sound's toggles (2023:0247, 0272, not
                 * sounded yet), and the menu waits on. */
                continue;
            }
            bool filtered = k == 0x1b || k == 0x60 || (space && k == ' ') ||
                            (dirs && ((k >= KEY_PAD5 && k <= KEY_UP) ||
                                      (k >= KEY_ALT + 'A' && k <= KEY_ALT + 'Z')));
            if (filtered) {
                result = k > 0xff ? (k & 0x7f) + 0x80 : k == 0x60 ? 0x1b : k;
                break;
            }
            for (size_t i = 0; i < count && picked < 0; ++i)
                if (hotkey(items[i][0], k)) picked = (int)i;
        }
        if (picked < 0) {
            beep(game);
            continue;
        }
        button(game, rows[picked], cols[picked], items[picked], (int)strlen(items[picked]),
               BUTTON_PRESSED);
        /* 1206: the item by its first letter, the first with it (af15). */
        result = picked;
        for (int i = 0; i < picked; ++i)
            if (items[i][0] == items[picked][0]) {
                result = i;
                break;
            }
        break;
    }
done:
    menu_wait_end(game);
    game->timed = false;
    controls_clear(game);
    return result;
}

/* An ECL menu's item as 6c70:0d51 has it: each letter in lower case, a
 * digit 0x20 on (a letter P-Y), a '~' dropped and the character after it
 * kept as it is (the hotkey, "~TALK" giving "Talk"). */
static void ecl_item(const char *s, char *out, size_t size)
{
    size_t n = 0;
    for (size_t i = 0; s[i] != '\0' && n + 1 < size; ++i) {
        char c = s[i];
        if (c == '~' && s[i + 1] != '\0') {
            out[n++] = s[++i];
            continue;
        }
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 0x20);
        else if (c >= '0' && c <= '9')
            c = (char)(c + 0x20);
        out[n++] = c;
    }
    out[n] = '\0';
}

/* Log a bar's items as the ECL builds them (6c70:1ff7: "%s~%s "). */
static void log_menu(cok_adventure *game, const char *const *items, size_t count)
{
    char line[512] = "";
    for (size_t i = 0; i < count; ++i) {
        size_t n = strlen(line);
        snprintf(line + n, sizeof line - n, "%s~%s", i > 0 ? " " : "", items[i]);
    }
    cok_adventure_log(game, "menu", line);
}

static void log_choice(cok_adventure *game, int choice)
{
    char text_[16];
    snprintf(text_, sizeof text_, "%d", choice);
    cok_adventure_log(game, "choice", text_);
}

/* 1bdb:0424: a question and Yes or No (1e29 with "Yes No", DS:4ee3, the
 * filter of the extended keys): Escape (or '`') No; the others ask
 * again. 1 Yes, 0 No, -1 if the keys ran out. */
static int yes_no(cok_adventure *game, const char *prompt, bool anim)
{
    static const char *const items[] = {"Yes", "No"};
    for (;;) {
        int choice = bar_menu(game, prompt, items, 2, true, anim, 0, 0);
        if (choice < 0) return -1;
        if (choice == 0) return 1;
        if (choice == 1 || choice == 0x1b) return 0;
    }
}

/* Screens. */

/* The full window (20b2:0030): cells 1-38 by 1-22 cleared, then FRAME's
 * top, sides and bottom. */
static void full_window(cok_adventure *game)
{
    clear_cells(game, 1, 1, 0x26, 0x16);
    for (unsigned i = 1; i <= 4; ++i) draw_frame_record(game, i, 0, 0, NULL);
}

/* The frame's colours 0x10-0x1f by variable 0x3a (20b2:047c): FRAME 43 for
 * 3, 42 for 4, 44 for 5 and 6, else 41. */
static void frame_palette_of(cok_adventure *game, unsigned style)
{
    unsigned id = style == 3 ? 43 : style == 4 ? 42 : style == 5 || style == 6 ? 44 : 41;
    const hlib_library *l = lib(game, LIB_FRAME);
    if (l != NULL) set_palette(game, l, id, 0);
}

static void frame_palette(cok_adventure *game)
{
    frame_palette_of(game, cok_ecl_load_var(&game->vm, 0x3a));
}

/* The 3D adventure screen (20b2:00e4): the full window, FRAME 7 under
 * rows 16 and 12, the view's frame (12), the three lamps (0x1d, 0x21,
 * 0x25 and variables 0x1fa-0x1fc), the compass's box (23) and its needle
 * by the facing's first letter (DS:6158: N 25, S 26, W 27, E 28). The
 * party's list and status (177e:000b, 5e22:023b, 03f8) are not drawn: the
 * port has no Dark Queen party yet. */
static void adventure_screen(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    frame_palette(game);
    full_window(game);
    draw_frame_record(game, 7, 8 * 16, 0, NULL); /* 20b2:000d(16) */
    draw_frame_record(game, 7, 8 * 12, 0, NULL);
    draw_frame_record(game, 12, 0, 0, NULL);
    for (unsigned i = 0; i < 3; ++i)
        draw_frame_record(game, 0x1d + 4 * i + cok_ecl_load_var(vm, (uint16_t)(0x1fa + i)), 0, 0,
                          NULL);
    draw_frame_record(game, 23, 0, 0, NULL);
    static const uint8_t needle[8] = {25, 25, 28, 26, 26, 26, 27, 25};
    draw_frame_record(game, needle[vm->direction & 7], 0, 0, NULL);
}

/* The overland's frame (20b2:02d3): the full window and FRAME 7 under row
 * 16, the map to come from the big picture. */
static void overland_screen(cok_adventure *game)
{
    full_window(game);
    draw_frame_record(game, 7, 8 * 16, 0, NULL);
}

/* The combat screen (20b2:02fa, the frame of 51c2:0c94's redraw, which
 * cok_arena_redraw makes by 1128:04c1): the panel cleared (1a62:09a1 with
 * 8, the window's grey: cells 23-38 by 1-21), FRAME 1, 2, 3 and 5, FRAME 4
 * within rows 186-199 (bottom_bar, 151e:06cb and 075c around it), FRAME 6.
 * The view's cells are left as they are: the ground is drawn over them
 * after this (cok_arena_centre). Its begin of the page (19a4:05ee) is the one screen's
 * (games.md's Dark Queen section). 1bdb:0d1d (DS:a6b5, DS:1d13: the bar drawn again by
 * 1bdb:0d26) is not needed, because the port's bar is always drawn whole. 0c94's next call,
 * 5837:00e4 (19a4:0630, DS:c5d4: the page's picture again, DS:a2ae and 1cb7 when it changes),
 * is not ported: it matters only after the later redraws. The panel's text is drawn by
 * combat_panel (cok_arena_panel) after this. */
static void combat_screen(cok_adventure *game)
{
    clear_cells(game, 23, 1, 38, 21);
    for (unsigned i = 1; i <= 3; ++i) draw_frame_record(game, i, 0, 0, NULL);
    draw_frame_record(game, 5, 0, 0, NULL);
    bottom_bar(game);
    draw_frame_record(game, 6, 0, 0, NULL);
}

/* The colour of a name (177e:1697 without the selection): black where the character cannot act
 * (+0x18b 0), green where it is hostile in combat (+0x66 1, mode 5), light magenta where it may
 * train (+0xcd), else grey. */
static unsigned name_colour(cok_adventure *game, const uint8_t *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (c[0x18b] == 0) return 0x00;
    if (cok_field_u8(L, c, COK_FIELD_HOSTILE) == 1 && game->vm.mode == 5) return 0x02;
    return c[0xcd] != 0 ? 0x0d : 0x07;
}

/* The status names by status 0-8 (177e:085f's pointers at DS:3bde: "Okay" ... "Gone"); 9 is
 * blank. Dark Queen's own table: not the strings at DS:1330 that rules.c gives the other games. */
static const char *const status_names[10] = {
    "Okay", "Animated", "tempgone", "Running", "Unconscious", "Dying", "Dead", "Petrified",
    "Gone", ""};

/* The side panel for c (177e:085f, cok_arena_panel's hook; 5045:0846 draws it at each turn's
 * start, after the cursor's box): cells 23-38 by 1-21 cleared; the name on row 1 (177e:156e's
 * say of " ", whose wrap of blanks is not drawn; the name by 177e:1697's colours); "Hitpoints"
 * (DS:023e) on row 3 and "AC" (DS:0248) on row 5 in grey at column 23; the hit points at column
 * 33 (177e:07b4: light cyan below the most, else grey) and the armour class at column 26
 * (177e:0724: 0x3c less +0x18e, a '-' before it above 60, grey; its trailing blank is not drawn).
 * The cursor's row is 5 (DS:ae4e). The status name in light cyan where the character cannot act,
 * else "(Helpless)" (DS:024b, 177e:0a19) or "(Casting)" (DS:0256) in light cyan while it casts a
 * spell (+0x40's byte), two rows below the readied weapon's name's last row (177e:0981 reads the
 * cursor row 177e:0311 left: row 7 with no weapon). The weapon's name (177e:03fc,
 * queen_item_name) is wrapped from row 7 by 177e:0311, drawn at once (no delay). */
static bool combat_panel(cok_adventure *game, cok_character *c)
{
    const cok_rules *R = cok_game_rules(game);
    const cok_character_layout *L = R->character;
    const uint8_t *r = c->record;
    clear_cells(game, 23, 1, 38, 21);
    char name[17];
    cok_field_name(L, r, name);
    text(game, 8, 8 * 23, name, 0x80 | name_colour(game, r));
    text(game, 8 * 3, 8 * 23, "Hitpoints", 0x87);
    unsigned hp = cok_field_u8(L, r, COK_FIELD_HIT_POINTS);
    unsigned most = cok_field_u8(L, r, COK_FIELD_MAX_HIT_POINTS);
    char line[16];
    snprintf(line, sizeof line, "%u", hp);
    text(game, 8 * 3, 8 * 33, line, 0x80 | (hp >= most ? 0x07 : 0x0b));
    text(game, 8 * 5, 8 * 23, "AC", 0x87);
    int value = cok_field_u8(L, r, COK_FIELD_AC) - 60;
    snprintf(line, sizeof line, "%s%u", value > 0 ? "-" : "",
             (unsigned)(value < 0 ? -value : value));
    text(game, 8 * 5, 8 * 26, line, 0x87);
    game->vm.cursor.y = 5;
    /* The readied weapon (slot 0, +0xc): its name by 177e:03fc (queen_item_name, as the sheet's),
     * wrapped by 177e:0311 in columns 23-38 over rows 7-9, colour 7. 085f:095e passes clear (1);
     * the delay between characters is only while DS:ae65 is set (0476), which 59c7:1de2 clears
     * for a battle, so the name is printed at once there. */
    size_t slot = c->slots[0];
    if (slot != 0 && slot <= c->item_count) {
        char weapon[96];
        queen_item_name(&game->party, c->items[slot - 1], false, weapon, sizeof weapon);
        if (!print_window(game, weapon, 23, 7, 38, 9, 7, true)) return false;
    }
    /* 177e:0981: the cursor row 0311 left, plus two. */
    int row = game->vm.cursor.y + 2;
    if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0) { /* 177e:085f's status line */
        int status = cok_field_s8(L, r, COK_FIELD_STATUS);
        if (status < 0 || status > 9)
            return cok_adventure_not_yet(game, "a status the original's table does not hold",
                                         "dqk 177e:085f (DS:3bde)");
        text(game, 8 * row, 8 * 23, status_names[status], 0x8b);
    } else if (cok_combat_helpless(c, R)) { /* 177e:0a19 */
        text(game, 8 * row, 8 * 23, "(Helpless)", 0x8b);
    } else if (cok_character_combat(c)->spell > 0) {
        text(game, 8 * row, 8 * 23, "(Casting)", 0x8b);
    }
    return true;
}

static void frame(cok_adventure *game, int screen)
{
    if (screen == COK_SCREEN_COMBAT)
        combat_screen(game);
    else
        adventure_screen(game);
}

/* 5e98:0200: the graphics' state to state, leaving the last one's
 * resources: the small picture (0, 2; and 0's wall sets, 8a8b:13ef, and
 * backdrop), the big picture (3). */
static void enter_state(cok_adventure *game, unsigned state)
{
    queen *q = own(game);
    if (state == q->state) return;
    if (q->state == STATE_PICTURE || q->state == 2) {
        q->picture_loaded = false;
        q->picture_id = 0xff;
    }
    if (q->state == STATE_PICTURE)
        for (unsigned slot = 0; slot < WALL_SETS; ++slot) q->wall_loaded[slot] = false;
    if (q->state == STATE_BIG) q->big_id = NO_BIG;
    q->state = state;
    if (state == STATE_VIEW)
        for (unsigned slot = 0; slot < WALL_SETS; ++slot) q->wall_loaded[slot] = false;
}

/* The 3D view (dqk 8a8b:085a). */

static const int8_t step_x[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};   /* DS:6189 */
static const int8_t step_y[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};   /* DS:6192 */

/* Load wall set id into slot (8a8b:119d): 8X8DB holds ids below 10, 8X8DC
 * the others; a slot past 0 takes its colours 0x20-0x44 at 0x20 + 37 *
 * slot, its palette record's first with them; the palette is set. */
static bool load_walls(cok_adventure *game, unsigned id, unsigned slot)
{
    queen *q = own(game);
    if (!sub_library(game, id < 10 ? LIB_WALLS_B : LIB_WALLS_C, id, &q->walls[slot]))
        return false;
    for (unsigned i = 0; i < 256; ++i) q->remap[slot][i] = (uint8_t)i;
    for (unsigned i = 0; i < WALL_COLOURS; ++i)
        q->remap[slot][0x20 + i] = (uint8_t)(0x20 + WALL_COLOURS * slot + i);
    set_palette(game, &q->walls[slot], 0, WALL_COLOURS * slot);
    q->wall_loaded[slot] = true;
    return true;
}

/* A piece of the wall of type wall (1-15: a slot's types 1-5) at view
 * column col and row row (8a8b:0435, 0346): place idx's piece, a door's
 * over its wall's. */
static void piece(cok_adventure *game, int col, int row, unsigned wall, unsigned idx)
{
    queen *q = own(game);
    static const cok_vga_clip clip = {VIEW_AT, VIEW_AT, VIEW_END, VIEW_END};
    int x = VIEW_AT + 8 * col, y = VIEW_AT + 8 * row;
    unsigned slot = 0, t = wall;
    while (t > 5) {
        t -= 5;
        ++slot;
    }
    unsigned p = idx + 1;
    if (p == 10) {
        p = 0;
        if (t > 2) t = 1;
    }
    if (p < 8) y += 8;
    --t;
    if (slot >= WALL_SETS || !q->wall_loaded[slot]) return;
    if (t < 2) {
        draw(game, &q->walls[slot], t * 10 + p + 1, y, x, &clip, q->remap[slot]);
    } else {
        draw(game, &q->walls[slot], p + 1, y, x, &clip, q->remap[slot]);
        draw(game, &q->walls[slot], t * 9 + p + 2, y, x, &clip, q->remap[slot]);
    }
}

/* The column and row of each place's pieces from the view's corner
 * (DS:9134-915a): far front, far left and right, middle front, left and
 * right, near front, left and right, and the far joins. */
enum { FAR_FRONT, FAR_LEFT, FAR_RIGHT, MID_FRONT, MID_LEFT, MID_RIGHT, NEAR_FRONT, NEAR_LEFT,
       NEAR_RIGHT, FAR_JOIN };
static const int8_t place_col[10] = {5, 4, 6, 4, 2, 7, 2, 0, 9, 5};
static const int8_t place_row[10] = {4, 3, 3, 3, 1, 1, 1, 0, 0, 4};

/* Place where's piece of a wall of type wall, shift columns on (DS:9134). */
static void place(cok_adventure *game, unsigned where, int shift, unsigned wall)
{
    piece(game, place_col[where] + shift, place_row[where], wall, where);
}

static void view_walls(cok_adventure *game, int px, int py, unsigned f)
{
    const cok_view *v = &game->view;
    unsigned l = (f + 6) % 8, r = (f + 2) % 8, b = (f + 4) % 8;
    int cx = px + 2 * step_x[f], cy = py + 2 * step_y[f];
#define W(x_, y_, d_) cok_view_wall(v, (d_), (x_), (y_))
    for (int d = 2; d >= 0; --d) {
        if (d == 2) {
            int x = cx, y = cy, si = 0;
            unsigned prev = 0;
            for (int k = 0; k < 4; ++k) { /* sweeping left */
                unsigned front = W(x, y, f);
                if (!cok_view_inside(v, x, y) && W(x, y, r) == 0) prev = 0;
                if (front) {
                    if (prev) place(game, FAR_JOIN, si + 1, prev);
                    prev = front;
                    if (k < 3) place(game, FAR_FRONT, si, front);
                } else {
                    if (prev && W(x - step_x[l], y - step_y[l], l))
                        place(game, FAR_JOIN, si + 1, prev);
                    prev = 0;
                }
                si -= 2;
                x += step_x[l];
                y += step_y[l];
            }
            x = cx, y = cy, si = 0, prev = 0;
            for (int k = 0; k < 4; ++k) { /* sweeping right */
                unsigned front = W(x, y, f);
                if (!cok_view_inside(v, x, y) && W(x, y, l) == 0) prev = 0;
                if (front) {
                    if (prev) place(game, FAR_JOIN, si - 1, prev);
                    prev = front;
                    if (k != 0 && k < 3)
                        place(game, FAR_FRONT, si, front);
                } else {
                    if (prev && W(x - step_x[r], y - step_y[r], r))
                        place(game, FAR_JOIN, si - 1, prev);
                    prev = 0;
                }
                si += 2;
                x += step_x[r];
                y += step_y[r];
            }
            x = cx, y = cy, si = 0;
            for (int k = 0; k < 3; ++k) { /* the far row's left sides */
                unsigned s = W(x, y, l);
                if (s) place(game, FAR_LEFT, si - (k ? 1 : 0), s);
                si -= 2;
                x += step_x[l];
                y += step_y[l];
            }
            x = cx, y = cy, si = 0;
            for (int k = 0; k < 3; ++k) { /* and its right sides */
                unsigned s = W(x, y, r);
                if (s) place(game, FAR_RIGHT, si + (k ? 1 : 0), s);
                si += 2;
                x += step_x[r];
                y += step_y[r];
            }
        } else if (d == 1) {
            int x = cx + 2 * step_x[l], y = cy + 2 * step_y[l], si = -6;
            for (int k = 0; k < 3; ++k) { /* from two to the left to the middle */
                unsigned front = W(x, y, f), s = W(x, y, l);
                if (front) place(game, MID_FRONT, si, front);
                if (k != 0 && s) place(game, MID_LEFT, si, s);
                si += 3;
                x += step_x[r];
                y += step_y[r];
            }
            x = cx + 2 * step_x[r], y = cy + 2 * step_y[r], si = 6;
            for (int k = 0; k < 3; ++k) { /* from two to the right */
                unsigned front = W(x, y, f), s = W(x, y, r);
                if (k < 2 && front)
                    place(game, MID_FRONT, si, front);
                if (k != 0 && s) place(game, MID_RIGHT, si, s);
                si -= 3;
                x += step_x[l];
                y += step_y[l];
            }
        } else {
            int x = cx + step_x[l], y = cy + step_y[l], si = -7;
            for (int k = 0; k < 2; ++k) { /* the party's row: left, then its own */
                unsigned front = W(x, y, f), s = W(x, y, l);
                if (front) place(game, NEAR_FRONT, si, front);
                if (k != 0 && s) place(game, NEAR_LEFT, si, s);
                si += 7;
                x += step_x[r];
                y += step_y[r];
            }
            x = cx + step_x[r], y = cy + step_y[r], si = 7;
            for (int k = 0; k < 2; ++k) {
                unsigned front = W(x, y, f), s = W(x, y, r);
                if (k < 1 && front)
                    place(game, NEAR_FRONT, si, front);
                if (k != 0 && s) place(game, NEAR_RIGHT, si, s);
                si -= 7;
                x += step_x[l];
                y += step_y[l];
            }
        }
        cx += step_x[b];
        cy += step_y[b];
    }
#undef W
}

/* The backdrop (8a8b:021c): the sky, horizon and ground filled (DS:ca0c,
 * c9f5, c9f6: never set from 0), then BACK's picture by the square: the
 * saved square's byte (vm->square, DS:c9f4, tested at 8a8b:0290 and 029d:
 * set at a block's entry, a step and a saved game's load, not by the
 * view) without 0x80 BACK 0x22 or, where the square's choice (8a8b:0807,
 * from the party's place) is not 0, the map's third; with 0x80 the map's
 * first, second or third by the choice, or none; ids from 0x20 one on at
 * night (game byte 8, the hour, below 6 or from 20). */
static void backdrop(cok_adventure *game, int x, int y)
{
    const cok_view *v = &game->view;
    fill(game, VIEW_AT, VIEW_AT, 68, VIEW_END, 0);
    fill(game, 68, VIEW_AT, 70, VIEW_END, 0);
    fill(game, 70, VIEW_AT, VIEW_END, VIEW_END, 0);
    uint8_t choice = cok_view_backdrop_choice(v, x, y);
    unsigned id;
    if (!(game->vm.square & 0x80))
        id = choice == 0 ? 0x22 : v->geo_header[7];
    else
        id = choice < 3 ? v->geo_header[5 + choice] : 0xff;
    unsigned hour = cok_ecl_load_var(&game->vm, 8);
    if (id >= 0x20 && id != 0xff && (hour < 6 || hour >= 20)) ++id;
    if (id == 0xff) return;
    hlib_library back;
    if (!sub_library(game, LIB_BACK, id, &back)) return;
    set_palette(game, &back, 0, 0);
    draw(game, &back, 1, VIEW_AT, VIEW_AT, NULL, NULL);
}

static void draw_view(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    const cok_view *v = &game->view;
    if (v->geo == NULL) return;
    /* The wall sets the map names (its header's bytes 2-4) when slot 0's
     * is not loaded. */
    if (!q->wall_loaded[0]) {
        for (unsigned slot = WALL_SETS; slot-- > 0;) {
            uint8_t id = v->geo_header[2 + slot];
            if (id != 0xff && !load_walls(game, id, slot)) return;
        }
    } else {
        /* A picture shown since takes colours 0x20-0xff (63ae:0078):
         * the sets' are set again. Where the original does so is not
         * found yet (8a8b:085a loads them only with slot 0's); the flow
         * dqk_ecl_text.py checks the view after a small picture. */
        for (unsigned slot = 0; slot < WALL_SETS; ++slot)
            if (q->wall_loaded[slot]) set_palette(game, &q->walls[slot], 0, WALL_COLOURS * slot);
    }
    backdrop(game, vm->map_x, vm->map_y);
    view_walls(game, vm->map_x, vm->map_y, vm->direction & 6);
}

static bool big_picture(cok_adventure *game, unsigned n);

/* The view again (5e22:0000): on the overland (variables 0x22 0 and 0x24
 * 1) its map, variable 0x3e's big picture, whole (without the party's
 * place: the callers that want it draw it); else, in mode 4 or in mode 3
 * with variable 0x24 not 1 (the mode test at 5e22:0032-0049), the 3D view
 * in its box, the rest of the screen as it is; in any other mode nothing.
 * The square's byte is the saved one (DS:c9f4), not recomputed here. Nothing while DS:b7a7 is
 * set (5e22:0000-0005: a lost party, a load asked), DS:ae27 (game->redraw) kept. */
static void view(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (game->party_killed || own(game)->b7a7) return;
    if (cok_ecl_load_var(vm, 0x22) == 0 && cok_ecl_load_var(vm, 0x24) == 1) {
        if (!big_picture(game, cok_ecl_load_var(vm, 0x3e)))
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
    } else if (vm->mode == 4 || (vm->mode == 3 && cok_ecl_load_var(vm, 0x24) != 1)) {
        draw_view(game);
    }
    game->redraw = false;
}


/* Opcodes. */

/* LOAD FILES map, wall set (6c70:1dec): the map from GEO.GLB
 * (8a8b:12e3: variables 0x13 and 0xc6 its id, slots 0 and 1 freed), 0x03
 * cleared; then the wall set (6c70:1e30): unless it is 0xff or the one
 * loaded (DS:ca03, saved with the game), its id is kept there and the big
 * picture is gone (19a4:0009 frees it, DS:ae43 is 0: 6c70:1e41-1e57). */
static void load_files(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    uint8_t map = (uint8_t)cok_ecl_value(vm, 0);
    cok_ecl_set_var(vm, COK_VAR_MAP, map);
    size_t size;
    uint8_t *data = cok_adventure_record(game, cok_game_rules(game)->archives.geo, vm->file, map,
                                         &size);
    if (data == NULL) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return;
    }
    bool ok = cok_view_set_geo(&game->view, data, size);
    free(data);
    if (!ok) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED,
                           "GEO.GLB record %u: Unable to load geo in Load3DMap", map);
        return;
    }
    cok_ecl_store(vm, 0xc6, map);
    q->wall_loaded[0] = q->wall_loaded[1] = false;
    cok_ecl_store(vm, 0x03, 0);
    uint8_t set = (uint8_t)cok_ecl_value(vm, 1);
    if (set != 0xff && set != game->wall_ids[0]) {
        game->wall_ids[0] = set;
        q->big_id = 0;
    }
    game->files_loaded = game->map_loaded = true;
}

/* The party's place on the overland map (7065:0011): ALWAYS 29 at the
 * cell past variables 0x25 and 0x26, the map's square. */
static void marker(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    int col = 8 * (cok_ecl_load_var(vm, 0x25) + 1), row = 8 * (cok_ecl_load_var(vm, 0x26) + 1);
    /* 19a4:09eb: the cell under it saved first. */
    int sw = (int)game->screen.units * 8;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            q->under[8 * y + x] = row + y < (int)game->screen.height && col + x < sw
                                      ? game->screen.pixels[(size_t)(row + y) * (size_t)sw +
                                                            (size_t)(col + x)]
                                      : 0;
    q->mark_x = col;
    q->mark_y = row;
    q->marked = true;
    draw(game, lib(game, LIB_ALWAYS), 0x1d, row, col, NULL, NULL);
}

/* 7065:0072: the cell the mark covers put back (19a4:0aba). */
static void unmark(cok_adventure *game)
{
    queen *q = own(game);
    if (!q->marked) return;
    int sw = (int)game->screen.units * 8;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (q->mark_y + y < (int)game->screen.height && q->mark_x + x < sw)
                game->screen.pixels[(size_t)(q->mark_y + y) * (size_t)sw +
                                    (size_t)(q->mark_x + x)] = q->under[8 * y + x];
    q->marked = false;
}

/* A big picture, BIGPIC's library n (63ae:0321, then 63ae:03b0): loaded
 * unless it is (DS:ae43), colours 0x20-0xff black first; the overland's
 * frame (20b2:02d3), its record 1 at cell 1, 1 (304 by 120: cells 1-38
 * by 1-15), its palette (record 0, with the water's cycles). The state
 * leaves the small picture and the view's wall sets. False with
 * game->error set where it is missing. */
static bool big_picture(cok_adventure *game, unsigned n)
{
    queen *q = own(game);
    enter_state(game, STATE_BIG);
    if (q->big_id != n) {
        if (!sub_library(game, LIB_BIGPIC, n, &q->big)) return false;
        black(game, 0x20);
        q->big_id = q->big_last = n;
    }
    overland_screen(game);
    draw(game, &q->big, 1, 8, 8, NULL, NULL);
    set_palette(game, &q->big, 0, 0);
    return true;
}

/* A small picture n (63ae:0078, from PICTURE and the encounter's close-up): PIC<A,
 * B or C>1, its frames' loop from DS:b20c, its colours 0x20-0xff black, record 1 drawn
 * (picture_frame), its palette; the picture shown. */
static void small_picture(cok_adventure *game, uint8_t n)
{
    queen *q = own(game);
    unsigned which = n < 0x80 ? LIB_PIC_A : n < 0x97 ? LIB_PIC_B : LIB_PIC_C;
    enter_state(game, STATE_PICTURE);
    if (q->picture_loaded && q->picture_id == n) {
        /* 63ae:00c4-00d9: the same library and n (DS:bd34, DS:ae42) skip the load, the
         * palette and the reset of DS:b20e (63ae:0299); 02ac draws record 1 only, and the
         * frame stays (177e:1b9b's 000c at frame 0 draws the same). */
        draw(game, &q->picture, 1, VIEW_AT, VIEW_AT, NULL, NULL);
        game->picture_shown = true;
        return;
    }
    q->picture_loaded = false;
    if (!sub_library(game, which, n, &q->picture)) {
        cok_adventure_log(game, "error", game->error);
        return;
    }
    q->picture_loaded = true;
    q->picture_id = n; /* DS:ae42 */
    /* Its frames are records 1 on (DS:b20d the last's index); the loop
     * goes back to frame 0, or 5 for pictures 0x0e and 0x87, 1 for 0x46,
     * 3 for 0xa1 (63ae:0262-028f: DS:b20c). */
    q->frame = 0;
    q->first_frame = n == 0x0e || n == 0x87 ? 5 : n == 0x46 ? 1 : n == 0xa1 ? 3 : 0;
    q->last_frame = (uint8_t)(q->picture.count >= 2 ? q->picture.count - 2 : 0);
    black(game, 0x20);
    picture_frame(game);
    game->picture_shown = true;
}

/* The encounter's sprite and close-up at distance (dqk 6c70:0255, which show_monster
 * calls where the game draws its own encounter). Nothing is shown once a close-up is
 * (DS:c6f6 byte 1: closeup_shown), nor for sprite 0xff. Otherwise: the overhead map
 * off and the sprite loaded, in a 3D area (sprite_loaded, sprite_shown: DS:b419), or
 * the view redrawn (DS:ae27) where it is loaded; then, in mode 4, the sprite's group
 * distance + 1 drawn over the view's box (53ae:02e5, the frame's offset as the small
 * picture's). A close-up
 * (the picture of closeup_id, small_picture) comes at distance 0, or where the sprite
 * is 0xff, and only in mode 4. The pointer's hide and show (19a4:061a, DS:1cb6) are
 * the front end's. */
static void encounter(cok_adventure *game, uint8_t distance)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    bool none = game->sprite_id == 0xff;
    if (!game->closeup_shown && !none) {
        if (!game->sprite_loaded) {
            cok_adventure_overhead_off(game); /* DS:ca07 */
            if (cok_ecl_var(vm, COK_VAR_3D) != 0) {
                /* 53ae:0078 with mode 1: the sprite's record, its palette set. */
                if (!sub_library(game, LIB_SPRIT, game->sprite_id, &q->sprite)) {
                    cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
                    return;
                }
                set_palette(game, &q->sprite, 0, 0);
                game->sprite_loaded = true;
                game->sprite_shown = true;
            }
        } else {
            game->redraw = true;
            cok_adventure_view(game); /* 5e22:0000 */
        }
        if (vm->mode == 4) {
            /* 53ae:02e5: group distance + 1 at the view's box (its record's hotspot applies). */
            draw(game, &q->sprite, distance + 1u, VIEW_AT, VIEW_AT, NULL, NULL);
            char text[64];
            snprintf(text, sizeof text, "sprite %u at %u", game->sprite_id, distance);
            cok_adventure_log(game, "monster", text);
        }
    }
    if (game->closeup_shown || game->closeup_id == 0xff) return;
    if ((distance != 0 && !none) || vm->mode != 4) return;
    small_picture(game, game->closeup_id); /* DS:ae64 (picture_shown) set there */
    game->closeup_shown = true;
}

/* PICTURE n (5f07:04a0): 0xff the view again where a picture or a sprite
 * was shown (5e22:0000, view: on the overland its map again), then the
 * sprite and the close-up forgotten; any other n sets the close-up flag
 * first. From 0xf0 a big picture (63ae:0321, 03b0), the party's place over
 * it on the overland (mode 3: 7065:0011), where the party's list is not
 * drawn either (DS:b256); below, a small picture, PIC<A, B or C>1
 * (63ae:0078: A below 0x80, B below 0x97), its colours 0x20-0xff black
 * first, record 1 drawn at cell 3, 3 (63ae:000c), then its palette. */
static void picture(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    uint8_t n = (uint8_t)cok_ecl_value(vm, 0);
    if (n == 0xff) {
        /* 5f07:0511-054b: the view again where a picture or a sprite covers it
         * (DS:ae64, b419), unless the last mode was 4 and the mode is not
         * (0511-051d); then the sprite and the close-up forgotten (DS:c6f6). */
        if ((vm->last_mode != 4 || vm->mode == 4) &&
            (game->picture_shown || game->sprite_shown)) {
            game->picture_shown = game->sprite_shown = false;
            view(game);
        }
        game->sprite_loaded = game->closeup_shown = false; /* DS:c6f6 */
        return;
    }
    game->closeup_shown = true; /* DS:c6f7 (5f07:04ae) */
    if (n >= 0xf0) {
        if (!big_picture(game, n)) {
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return;
        }
        if (vm->mode == 3)
            marker(game);
        else
            q->big_no_party = true; /* DS:b256 */
        game->picture_shown = true;
        return;
    }
    small_picture(game, n);
}

/* CALL 3 (5f07:0aba): the small picture's frame drawn (63ae:000c, nothing
 * where none is loaded, as after a big picture), then the next frame,
 * past the last the loop's first; then the speed's delay, 100 ms times
 * variable 0x12 (1a62:0949), picture or not. The frame's redraw it asks
 * for where the loop's first is not 0 (20b2:022c with 1: DS:58ca) is not
 * drawn here. */
static void animate(cok_adventure *game)
{
    queen *q = own(game);
    if (q->picture_loaded) picture_frame(game);
    next_frame(q);
    pass(game, 100u * cok_ecl_load_var(&game->vm, 0x12));
}

/* HORIZONTAL MENU and ASK MENU (6c70:1ff7 with 0 or 1, 6c70:0e3f): the
 * items (6c70:0d51's forms) on the bar, read with the extended keys'
 * filter, the small picture animating where one is shown (DS:ae64);
 * Escape and the filter's other keys ask again (its escape argument 0;
 * Up and Down would change the selected member: no party yet); the index
 * stored, the bar drawn again. */
static void horizontal(cok_adventure *game, bool ask)
{
    cok_ecl *vm = &game->vm;
    /* In colour 7 whatever variable 0x39 says (6c70:200e pushes 7, as
     * JOURNAL's 6c70:330c does; PRINT's 3 is not its colour). */
    if (ask && !print_colour(game, "what do you do?", false, 7)) return; /* DS:7791 */
    const char *items[COK_ECL_MAX_STRINGS] = {NULL}; /* none read where count is 0 */
    char forms[COK_ECL_MAX_STRINGS][64];
    size_t count = vm->operands < COK_ECL_MAX_STRINGS ? vm->operands : COK_ECL_MAX_STRINGS;
    for (size_t i = 0; i < count; ++i) items[i] = vm->string[i];
    log_menu(game, items, count);
    for (size_t i = 0; i < count; ++i) {
        ecl_item(vm->string[i], forms[i], sizeof forms[i]);
        items[i] = forms[i];
    }
    int choice;
    do {
        choice = bar_menu(game, "", items, count, true, game->picture_shown, 0, 0);
        if (choice < 0) return;
    } while ((size_t)choice >= count);
    log_choice(game, choice);
    cok_ecl_store_as(vm, &vm->header[0], (uint16_t)choice);
    bottom_bar(game);
}

enum { LIST_BAR = 10 }; /* the buttons of a list's bar (Items' are ten) */

/* The list (1bdb:1f25, 1b05:08b3, 0055, keys 0664) of count items in
 * rows y1 to y2 - 1 from cell 1 to x2 (one less with more items than rows,
 * room for the scroll bar), each row cleared, the current one (*at) in
 * light cyan (0x8b), the others 0x87, under the bar's menu (1bdb:0d26:
 * the prompt boxed, then its items' buttons): Up or Home the one before,
 * Down or End the one after, wrapping; PgUp the top row shown or a page up
 * from it, PgDn the bottom one or a page down; the rows scrolling to the
 * current one. A bar item's letter (or a click on its button) ends it,
 * giving its index; Return its first; Escape (or '`') 0x1b; a click on a
 * row makes it current, on the current one its first item. Returns that,
 * -1 if the keys ran out. The scroll bar (21e9:082d) is not decoded nor
 * drawn. */
static int list_menu(cok_adventure *game, const char *prompt, const char *const *bar,
                     size_t bar_count, const char *const *items, size_t count, size_t *at,
                     int x1, int y1, int y2, int x2)
{
    int rows = y2 - y1, cols = x2 - 1;
    if (rows < 1) rows = 1;
    if ((size_t)rows < count) --cols; /* room for the scroll bar */
    size_t top = 0;
    if (*at >= count) *at = 0;
    char box[96];
    snprintf(box, sizeof box, "%s", prompt);
    size_t n = strlen(box);
    if (n >= 2 && box[n - 1] == ' ' && box[n - 2] == ':') box[--n] = '\0';
    bottom_bar(game);
    if (n > 0) prompt_box(game, 0, box, 0, 7);
    controls_clear(game);
    rect rects[LIST_BAR];
    int cols_at[LIST_BAR], col = 8 + 8 * (int)n;
    for (size_t i = 0; i < bar_count && i < LIST_BAR; ++i) {
        int width = (int)strlen(bar[i]);
        cols_at[i] = col;
        rects[i] = button_rect(BAR_ROW, col, width);
        control_add(game, rects[i]);
        button(game, BAR_ROW, col, bar[i], width, 0);
        col += (width + 1) * 8;
    }
    rect list = {8 * y1, 8 * x1, 8 * (y1 + rows), 8 * (1 + cols)};
    control_add(game, list);
    int result = -1;
    while (result < 0) {
        if (*at < top) top = *at;
        if (*at >= top + (size_t)rows) top = *at + 1 - (size_t)rows;
        for (int i = 0; i < rows; ++i) {
            clear_cells(game, x1, y1 + i, cols, y1 + i);
            size_t item = top + (size_t)i;
            if (item < count) text(game, 8 * (y1 + i), 8 * x1, items[item], item == *at ? 0x8b : 0x87);
        }
        input in;
        int key = read_input(game, &in);
        if (key < 0 && key != COK_KEY_TIMEOUT) {
            controls_clear(game);
            return -1;
        }
        if (in.click) {
            bool taken = false;
            for (size_t i = 0; i < bar_count && i < LIST_BAR && !taken; ++i)
                if (inside(&rects[i], in.x, in.y)) {
                    result = (int)i;
                    taken = true;
                }
            if (!taken && inside(&list, in.x, in.y) && count > 0) {
                size_t item = top + (size_t)((in.y - 8 * y1) / 8);
                if (item >= count) item = count - 1;
                if (item == *at) result = 0;
                *at = item;
            } else if (!taken) {
                beep(game);
            }
            continue;
        }
        size_t page = rows > 1 ? (size_t)rows - 1 : 1, bottom = top + (size_t)rows - 1;
        if (bottom >= count) bottom = count > 0 ? count - 1 : 0;
        switch (in.key) {
        case KEY_UP: case KEY_HOME: *at = *at > 0 ? *at - 1 : count - 1; break;
        case KEY_DOWN: case KEY_END: *at = *at + 1 < count ? *at + 1 : 0; break;
        case KEY_PGUP: *at = *at != top ? top : *at > page ? *at - page : 0; break;
        case KEY_PGDN:
            *at = *at != bottom ? bottom : *at + page < count ? *at + page : count - 1;
            break;
        case '\r': case '\n': result = 0; break;
        case 0x1b: case 0x60: result = 0x1b; break;
        case KEY_ALT + 'S': case KEY_ALT + 'T': break; /* the sound's toggles */
        default: {
            bool taken = false;
            for (size_t i = 0; i < bar_count && i < LIST_BAR && !taken; ++i)
                if (hotkey(bar[i][0], in.key)) {
                    result = (int)i;
                    taken = true;
                }
            if (!taken) beep(game);
            break;
        }
        }
        if (count == 0) *at = 0;
    }
    controls_clear(game);
    if (result != 0x1b)
        button(game, BAR_ROW, cols_at[result], bar[result], (int)strlen(bar[result]),
               BUTTON_PRESSED);
    return result;
}

/* VERTICAL MENU (6c70:1e91, 0f4a, 1bdb:1f25): the prompt printed as
 * PRINTCLEAR, the items in list_menu's rows from the row below it to row
 * 21, "Select" on the bar (its 'S', Return its first). Select, or Escape
 * (or '`'), ends it, Select drawn pressed and the current one's index
 * stored either way, as wide as the first item (a string: a word; 1e91
 * reads DS:b216 in its item loop, the first item's type once the items
 * are loaded); then the window cleared. */
static void vertical(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    size_t count = vm->operands < COK_ECL_MAX_STRINGS ? vm->operands : COK_ECL_MAX_STRINGS;
    cok_adventure_log(game, "list", vm->header_string);
    for (size_t i = 0; i < count; ++i) cok_adventure_log(game, "item", vm->string[i]);
    vm->cursor = (cok_text_cursor){1, 0x11};
    if (!print_window(game, vm->header_string, 1, 0x11, 0x26, 0x16, 7, true)) return;
    const char *items[COK_ECL_MAX_STRINGS];
    for (size_t i = 0; i < count; ++i) items[i] = vm->string[i];
    static const char *const select[] = {"Select"}; /* DS:4eea */
    size_t at = 0;
    int y1 = vm->cursor.y + 1;
    int ended = list_menu(game, "", select, 1, items, count, &at, 1, y1, 0x16, 0x26);
    if (ended < 0) return;
    if (ended == 0x1b) button(game, BAR_ROW, 8, select[0], 6, BUTTON_PRESSED);
    log_choice(game, (int)at);
    cok_ecl_operand o = vm->header[0];
    if (count > 0) o.type = vm->operand[0].type;
    cok_ecl_store_as(vm, &o, (uint16_t)at);
    clear_cells(game, 1, 0x11, 0x26, 0x16);
    bottom_bar(game);
}

/* The line editor (6431:0001) in the bar's field (1a62:05c9, 0781: the box
 * of 38 cells from cell 1, 1a62:0046's), fg 7 on the window's grey, the
 * text from cell 2 of row 189 with its caret, a cell in fg past it (it
 * blinks: shown here as it is drawn first), the mouse's cursor hidden
 * while it reads (as the original's screens show it). Backspace or Left
 * takes the last character off; Return (or 0x0a) ends it, 1; Escape (or
 * '`') 0; a character 0x20-0x7e goes on while fewer than most; the rest,
 * and clicks, beep. -1 if the keys ran out. */
static int read_line_(cok_adventure *game, char *line, size_t most);
static int read_line_prompt(cok_adventure *game, const char *prompt, unsigned fg, unsigned bg,
                            char *line, size_t most);

static int edit_line(cok_adventure *game, char *line, size_t most)
{
    queen *q = own(game);
    bool shown = q->cursor_shown;
    q->cursor_shown = false;
    int result = read_line_(game, line, most);
    q->cursor_shown = shown;
    return result;
}

static int read_line_(cok_adventure *game, char *line, size_t most)
{
    return read_line_prompt(game, "", 7, 8, line, most);
}

/* The field with a prompt (1a62:05c9 with one): the box of 38 cells in
 * fg on bg, the prompt from cell 1 and the text after it. */
static int read_line_prompt(cok_adventure *game, const char *prompt, unsigned fg, unsigned bg,
                            char *line, size_t most)
{
    char blank[39];
    memset(blank, ' ', 38);
    blank[38] = '\0';
    size_t n = 0;
    line[0] = '\0';
    prompt_box(game, 0, blank, fg, bg);
    int at = prompt[0] == '\0' ? 16 : 8 + 8 * (int)strlen(prompt);
    if (prompt[0] != '\0') text(game, 189, 8, prompt, (bg & 15) << 4 | (fg & 15));
    for (;;) {
        char shown[48];
        snprintf(shown, sizeof shown, "%s ", line);
        text(game, 189, at, shown, (bg & 15) << 4 | (fg & 15));
        fill(game, 189, at + 8 * (int)n, 197, at + 8 + 8 * (int)n, colour(fg));
        input in;
        int key = read_input(game, &in);
        if (key < 0 && key != COK_KEY_TIMEOUT) return -1;
        if (in.click) {
            beep(game);
            continue;
        }
        if (in.key == 0x08 || in.key == KEY_LEFT) {
            if (n > 0) line[--n] = '\0';
        } else if (is_return(in.key)) {
            text(game, 189, at, shown, (bg & 15) << 4 | (fg & 15));
            snprintf(shown, sizeof shown, "%s ", line);
            text(game, 189, at, shown, (bg & 15) << 4 | (fg & 15));
            return 1;
        } else if (in.key == 0x1b || in.key == 0x60) {
            return 0;
        } else if (in.key >= 0x20 && in.key <= 0x7e && n < most) {
            line[n++] = (char)in.key;
            line[n] = '\0';
        } else {
            beep(game);
        }
    }
}

/* 2d84:000b: a number from text. A last B, D, H or O (either case) gives
 * its base (2, 10, 16, 8) and is not read; else 10. Then each character
 * in turn: a digit, or a letter, below the base adds to it (times the
 * base, in 16 bits); '-' makes it negative; one up to 0x20 is passed
 * over; any other ends it. An empty text is 0 (the original looks at the
 * byte before it for its last: not a letter of these, as its buffer's
 * neighbour on the stack is not one, it reads nothing). */
static int16_t parse_number(const char *text)
{
    size_t n = strlen(text);
    unsigned base = 10;
    if (n > 0) {
        static const char suffix[] = "BDHObdho";
        static const unsigned bases[] = {2, 10, 16, 8, 2, 10, 16, 8};
        const char *at = strchr(suffix, text[n - 1]);
        if (at != NULL && *at != '\0') {
            base = bases[at - suffix];
            --n;
        }
    }
    uint16_t value = 0;
    bool negative = false;
    for (size_t i = 0; i < n; ++i) {
        int c = (signed char)text[i];
        unsigned d;
        if (c >= '0' && c < (int)(base + '0'))
            d = (unsigned)(c - '0');
        else if (c >= 'A' && c < (int)(base + 0x37))
            d = (unsigned)(c - 0x37);
        else if (c >= 'a' && c < (int)(base + 0x57))
            d = (unsigned)(c - 0x57);
        else if (c == '-') {
            negative = true;
            continue;
        } else if (c <= 0x20) {
            continue;
        } else {
            break;
        }
        value = (uint16_t)(value * base + d);
    }
    return (int16_t)(negative ? (uint16_t)-value : value);
}

/* INPUT NUMBER (6c70:2e7c, 1a62:0781, 6431:03ba): five characters at most
 * (32767's digits), read as 2d84:000b reads it, asked again (with a beep)
 * until 0-32767; Escape asks again too. Its first operand is not read. */
static void input_number(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    for (;;) {
        char line[8];
        int done = edit_line(game, line, 5);
        if (done < 0) return;
        if (done == 0) continue;
        int16_t value = parse_number(line);
        if (value < 0) {
            beep(game);
            continue;
        }
        char text_[24];
        snprintf(text_, sizeof text_, "%d", value);
        cok_adventure_log(game, "input", text_);
        cok_ecl_store_to(vm, 1, (uint16_t)value);
        return;
    }
}

/* INPUT STRING (6c70:2ec8, 1a62:05c9): 35 characters at most, upper case
 * (10d7:25fb), stored whether Return or Escape ended it (DS:a62a is not
 * read), " " for none (DS:775f). */
static void input_string(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    char line[40];
    if (edit_line(game, line, 35) < 0) return;
    for (char *c = line; *c != '\0'; ++c)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 0x20);
    if (line[0] == '\0') snprintf(line, sizeof line, " ");
    cok_adventure_log(game, "input", line);
    cok_ecl_store_string(vm, cok_ecl_address(vm, 1), line);
}

/* 6523:000c(n): wait n hundredths of the game's clock (1a62:0979, the
 * timer's ticks * 100 / 72: 800 is 7.91 seconds), each step polling the
 * keyboard and running the cycles (004c); a key read (251e:00e7's code)
 * ends it, but Alt-S and Alt-T (0x152, 0x153: the sound's toggles,
 * 2023:0247, 0272, not sounded yet) wait on, and clicks are not read
 * (251e:00dc is the keyboard's). Returns the code's low byte (its AL),
 * Space (0x20) when the time ran out; -1 if the keys ran out. */
static int timed_wait(cok_adventure *game, unsigned n)
{
    unsigned steps = (unsigned)((n * 72u * 1000u / 100u) * 16384u / 1193182u / 10u);
    for (unsigned waited = 0; waited < steps; ++waited) {
        if (cok_adventure_key_pending(game)) {
            input in;
            int key = read_input(game, &in);
            if (key < 0 && key != COK_KEY_TIMEOUT) return -1;
            if (!in.click && in.key != KEY_ALT + 'S' && in.key != KEY_ALT + 'T')
                return in.key & 0xff;
        }
        if (game->vm.abort) return -1;
        wait_cycles(game, 10);
    }
    return 0x20;
}

/* The routine PROGRAM n stops at (5f07:0baf's cases; the ones not ported). */
static const char *program_cite(unsigned n)
{
    switch (n) {
    case 3: return "dqk 78e4:17dd, a level drain";
    default: return "dqk 5f07:0baf";
    }
}

/* The ending's scenes (6523:03cb, PROGRAM 1), as its code runs them:
 * a scene's FINAL library (LOAD), a drawing at cell 1, 1 of its record 1
 * and over it another (DRAW n; 0 none: 6523:02db), a wait of n times 36
 * hundredths (WAIT: 6523:02c3, the frame shown, then 6523:000c). */
enum { END, LOAD, DRAW, WAIT, REPEAT, AGAIN };
static const uint8_t ending_steps[] = {
    LOAD, 1, DRAW, 0, WAIT, 1,
    DRAW, 2, WAIT, 1, DRAW, 3, WAIT, 1, DRAW, 4, WAIT, 1, DRAW, 5, WAIT, 1, DRAW, 6, WAIT, 1,
    DRAW, 7, WAIT, 1, DRAW, 8, WAIT, 1, DRAW, 9, WAIT, 1, DRAW, 10, WAIT, 1, DRAW, 11, WAIT, 1,
    DRAW, 12, WAIT, 1, DRAW, 13, WAIT, 1, DRAW, 14, WAIT, 1, DRAW, 15, WAIT, 1, DRAW, 16, WAIT, 1,
    WAIT, 1,
    LOAD, 3, DRAW, 0, WAIT, 2,
    DRAW, 2, WAIT, 2, DRAW, 3, WAIT, 2, DRAW, 4, WAIT, 2, DRAW, 5, WAIT, 2, DRAW, 6, WAIT, 2,
    DRAW, 5, WAIT, 1, DRAW, 7, WAIT, 2,
    DRAW, 8, DRAW, 9, DRAW, 10, DRAW, 11, DRAW, 12, DRAW, 13, DRAW, 14, DRAW, 15,
    REPEAT, 3, DRAW, 16, WAIT, 1, DRAW, 17, WAIT, 1, AGAIN,
    DRAW, 16, WAIT, 3,
    LOAD, 2, DRAW, 0,
    REPEAT, 6, DRAW, 1, WAIT, 1, DRAW, 2, WAIT, 1, DRAW, 3, WAIT, 1, AGAIN,
    DRAW, 4, WAIT, 1,
    DRAW, 1, DRAW, 3, WAIT, 1, DRAW, 4, DRAW, 1, WAIT, 1, DRAW, 4, DRAW, 1, WAIT, 1,
    DRAW, 2, DRAW, 5, WAIT, 1, DRAW, 3, DRAW, 5, WAIT, 1, DRAW, 1, DRAW, 6, WAIT, 1,
    DRAW, 2, DRAW, 6, WAIT, 1, DRAW, 3, DRAW, 7, WAIT, 1, DRAW, 1, DRAW, 7, WAIT, 1,
    DRAW, 2, DRAW, 8, WAIT, 1, DRAW, 3, DRAW, 8, WAIT, 1, DRAW, 1, DRAW, 9, WAIT, 1,
    DRAW, 2, DRAW, 9, WAIT, 1, DRAW, 3, DRAW, 10, WAIT, 1, DRAW, 1, DRAW, 10, WAIT, 1,
    DRAW, 2, DRAW, 11, WAIT, 1, DRAW, 3, DRAW, 12, WAIT, 1,
    DRAW, 13, WAIT, 2, DRAW, 14, WAIT, 2, DRAW, 13, WAIT, 1, DRAW, 14, WAIT, 2,
    END,
};

/* PROGRAM 1 (5f07:0baf's case 1, 6523:03cb): the ending, FINAL.TLB's
 * three scenes in the overland's frame (20b2:02d3), song 39 (2023:0006,
 * not played yet), each scene's palette set as it loads, no colours black
 * first; the last frame stays. */
static void ending(cok_adventure *game)
{
    queen *q = own(game);
    enter_state(game, STATE_TITLE);
    overland_screen(game);
    q->cursor_shown = false; /* 10d7:0141, shown again at the end (22b7:2122) */
    cok_adventure_log(game, "music", "39");
    hlib_library scene = {0};
    bool loaded = true;
    size_t loop = 0;
    unsigned repeats = 0;
    for (size_t i = 0; ending_steps[i] != END && loaded && !game->vm.abort; ++i) {
        switch (ending_steps[i]) {
        case LOAD:
            loaded = sub_library(game, LIB_FINAL, ending_steps[++i], &scene);
            if (!loaded) {
                cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
                break;
            }
            set_palette(game, &scene, 0, 0);
            break;
        case DRAW: {
            unsigned over = ending_steps[++i];
            draw(game, &scene, 1, 8, 8, NULL, NULL);
            if (over != 0) draw(game, &scene, over, 8, 8, NULL, NULL);
            break;
        }
        case WAIT:
            if (timed_wait(game, 36u * ending_steps[++i]) < 0) loaded = false;
            break;
        case REPEAT:
            repeats = ending_steps[++i];
            loop = i;
            break;
        case AGAIN:
            if (--repeats > 0) i = loop;
            break;
        default: break;
        }
    }
    q->cursor_shown = true;
}

static bool pass_time(cok_adventure *game, unsigned count, unsigned unit);
static cok_ecl_status camp(cok_adventure *game, bool from_main, bool *begin);
static void party_list(cok_adventure *game);
static void status_line(cok_adventure *game);
static bool view_character(cok_adventure *game, bool *done);
static bool save_game(cok_adventure *game);
static void say(cok_adventure *game, int x, int y, const char *s, unsigned fg);
static cok_ecl_status encamp(cok_adventure *game);

/* The camp's screen of the adventure (177e:1ad1 in mode 2, 1b16-1b5a): the
 * adventure's screen, the camping's small picture 0x80 (63ae:0078, its frame
 * at cell 3, 3 by 63ae:000c), the party's list and the status. */
static void encamp_screen(cok_adventure *game)
{
    adventure_screen(game);
    small_picture(game, 0x80);
    party_list(game);
    status_line(game);
}

/* The screen as it was (177e:1ad1) by the mode (DS:ae57; 177e:1aea's table,
 * none above 10): 0 the full window (10b2:0030); 3 the view again
 * (5e22:0000), where picture 9 is not shown and variable 0x24 is 1
 * (177e:1b82); 4 the adventure's screen, the view, the party's list and the
 * status (177e:1b5d). Mode 2 is the camp's (encamp_screen). Mode 6 is the treasure's
 * (177e:1b9b): the adventure's screen, the small picture 0x81 (63ae:0078, then 000c at
 * 3, 3) and the list, no status. Modes 1 and 10 first draw the adventure's screen
 * (10b2:00e4; in 1 and 10 only where DS:bee9 is set) and a small picture, then the
 * list and the status: not ported, so they stop. Other modes draw nothing. False where
 * the run stopped. DS:beea (set to 0 in mode 4) is read by LOAD FILES' redraw only, not
 * ported. */
static bool screen_as_it_was(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    queen *q = own(game);
    game->redraw = true; /* 177e:1ad1's first store, DS:ae27 (no reader of it here) */
    switch (vm->mode) {
    case 0:
        full_window(game);
        return true;
    case 3:
        if (q->picture_id != 9 && cok_ecl_load_var(vm, 0x24) == 1) view(game);
        return true;
    case 2:
        encamp_screen(game);
        return true;
    case 4:
        adventure_screen(game);
        view(game);
        party_list(game);
        status_line(game);
        return true;
    case 6: {
        bool shown = game->picture_shown;
        adventure_screen(game);
        small_picture(game, 0x81);
        game->picture_shown = shown; /* 63ae:0078 never writes DS:ae64 */
        party_list(game);
        return true;
    }
    case 1: case 10:
        return cok_adventure_not_yet(game, "the screen as it was in modes 1 and 10 "
                                           "(a small picture, DS:bee9)", "dqk 177e:1ad1");
    default:
        return true;
    }
}

/* PROGRAM 0 (5f07:0c15): the camp from the adventure (7368:046f with 0),
 * then, if its menu ended with Begin Adventuring, the screen as it was
 * (screen_as_it_was) and the party's place on the overland (7065:0011) where
 * picture 9 is not shown and variables 0x22 0, 0x24 1, and the view again
 * (DS:b7f8). Then the ECL run goes on only while DS:b7a7 is clear (5f07:0fc4,
 * the camp's Remove of the last member sets it): with the party emptied the run
 * stops (5ebd:039b, not ported); with a load asked it halts (vm->halt) and the
 * adventure's loop loads it (its tail). DS:c2dc (cleared by a new game, 5f07:10d3)
 * is taken as 0: nothing in the game sets it (only 5f07:10d3 clears it). */
static void camp_program(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    bool begin;
    cok_ecl_status status = camp(game, false, &begin);
    if (status != COK_ECL_OK || vm->abort) return;
    queen *q = own(game);
    if (begin) {
        if (q->picture_id != 9 && cok_ecl_load_var(vm, 0x22) == 0 &&
            cok_ecl_load_var(vm, 0x24) == 1) {
            if (!screen_as_it_was(game)) return;
            marker(game);
        }
        vm->view_changed = true;
    }
    if (q->b7a7 && !q->load_asked) {
        /* The party emptied (7368:06dc): the run is left, and main makes a new game and
         * shows its menu (5ebd:039b), not ported. */
        cok_adventure_not_yet(game, "the party emptied in the adventure's camp: main's new "
                                    "game and menu", "dqk 5ebd:039b");
        return;
    }
    if (q->b7a7) vm->halt = true; /* a load asked: the adventure's loop loads it */
}

static void remove_selected(cok_adventure *game, bool counted);
static void bar_message(cok_adventure *game, const char *s);

/* Alt's items (7070:1ce2; DS:5220): "Order Drop Speed Icon Level Exit". */
enum { ALT_ORDER, ALT_DROP, ALT_SPEED, ALT_ICON, ALT_LEVEL, ALT_EXIT, ALT_ITEMS };
static const char *const alt_words[ALT_ITEMS] = {"Order", "Drop", "Speed", "Icon", "Level",
                                                 "Exit"};

/* The filtered keys the list's selection takes (5e22:067c's table: Down 0x84, End 0x85,
 * Home 0x87, Up 0x88; Left and the rest do nothing). */
static bool list_key(int key)
{
    return key == 0x84 || key == 0x85 || key == 0x87 || key == 0x88;
}

/* 5e22:067c: a list key picks the member selected (vm->character): Down and End the next,
 * the head after the last; Up and Home the previous, the last before the head. Then the
 * list is drawn again (5e22:0065's two rows). With the list empty, nothing. Other keys do
 * nothing. Only the treasure menu and the temple use it so far: the other callers stop,
 * cited ("a member picked by a key"). */
static void member_key(cok_adventure *game, int key)
{
    cok_ecl *vm = &game->vm;
    if (!list_key(key) || game->party.count == 0) return;
    size_t i = cok_party_index(&game->party, vm->character), n = game->party.count;
    if (key == 0x84 || key == 0x85) i = i + 1 < n ? i + 1 : 0;
    else i = i > 0 && i < n ? i - 1 : n - 1;
    vm->character = cok_party_record(&game->party, i);
    party_list(game);
}

/* The selected member's name line and the text under it (177e:156e, the camp's shape:
 * 177e:1665's window cleared from row 17, the name on 18, the text on 19 in colour 7).
 * The Dark Queen camp is never resting, so the window starts at row 17. A NULL member
 * gives the text alone (its name line is skipped). The text's window is cleared and its cursor
 * homed (1a62:0311's bp+0x12 is 1 at 177e:1631). */
static void member_say(cok_adventure *game, const uint8_t *c, const char *text)
{
    clear_cells(game, 1, 0x11, 0x26, 0x16);
    size_t i = cok_party_index(&game->party, c);
    if (i < game->party.count) queen_name_line(game, game->party.members[i], 1, 0x12, false);
    print_window(game, text, 1, 0x13, 0x26, 0x16, 7, true);
}

void cok_dark_queen_member_say_for_test(cok_adventure *game, const uint8_t *c, const char *text)
{
    member_say(game, c, text);
}

/* Set the speed: the variable 0x12 the Speed menu reads and the text's delay reads
 * (1a62:0311), and the game's speed the waits use. */
static void set_speed(cok_adventure *game, unsigned speed)
{
    game->speed = (uint8_t)speed;
    cok_ecl_set_var(&game->vm, COK_VAR_SPEED, (uint16_t)speed);
}

/* Alt's Order (7070:19a3, the party's list by 5e22:023b). The bar's Select (or Place
 * while a member is moving) and Exit: Select toggles the moving state, which says "has
 * been selected" over the member while on, and clears the text while off. While moving,
 * Down or End moves the member down the party and Up or Home up (179b, 18b6: the
 * party's order wraps, as cok_party_move does), and the list is drawn again. Escape
 * leaves, after the list's redraw. Not moving, Down, End, Home and Up change the
 * member selected (5e22:067c): not ported, so it stops. False where the run stopped. */
static bool alt_order(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    bool moving = false;
    for (;;) {
        const char *items[] = {moving ? "Place" : "Select", "Exit"};
        log_menu(game, items, 2);
        int choice = bar_menu(game, "party order ", items, 2, true, true, 0, 0);
        if (choice < 0) return false;
        log_choice(game, choice);
        if (choice == 0x1b) {
            party_list(game);
            return true;
        }
        if (choice >= 0x80) {
            if (!moving && list_key(choice))
                return cok_adventure_not_yet(game, "a member picked by a key in the adventure's "
                                                   "camp's Order", "dqk 5e22:067c");
            if (moving && list_key(choice)) {
                size_t i = cok_party_index(&game->party, vm->character);
                if (i == game->party.count) {
                    cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                       "moving a character not in the party (7070:179b)");
                    return false;
                }
                cok_party_move(&game->party, i, choice == 0x84 || choice == 0x85);
            }
            party_list(game);
            continue;
        }
        if (choice == 1) return true;
        moving = !moving;
        if (moving) member_say(game, vm->character, "has been selected");
        else clear_cells(game, 1, 0x12, 0x26, 0x16);
    }
}

/* Alt's Drop (7368:0ee9): "Drop <name> forever? " and "Are you sure? " by Yes or No
 * (1bdb:0424, without the animation); No to either is "<name> breathes a sigh of
 * relief." The member removed (177e:1e31 with 0, 1) after "<name> bids you farewell."
 * (active, +0x18b) or "You dump <name> out back.", and the list drawn. With no member
 * selected the list alone is drawn. 177e:1e31 also calls 177e:1c87, which only gives
 * the member's blocks back to their pools (memory; cok_party_remove frees them too).
 * The saved game after a removal holds the original's heap past the item table
 * (604b:3352-3366 pads from DS:c6de; the port writes 0): dqk_camp.py masks those
 * bytes. False where the run stopped. */
static bool alt_drop(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (vm->character == NULL) {
        party_list(game);
        return true;
    }
    char name[17], text[64];
    cok_field_name(L, vm->character, name);
    snprintf(text, sizeof text, "Drop %s forever? ", name);
    int yes = yes_no(game, text, false);
    if (yes < 0) return false;
    if (yes) {
        yes = yes_no(game, "Are you sure? ", false);
        if (yes < 0) return false;
    }
    if (!yes) {
        snprintf(text, sizeof text, "%s breathes a sigh of relief.", name);
        bar_message(game, text);
    } else {
        bool active = cok_field_u8(L, vm->character, COK_FIELD_ACTIVE) != 0;
        snprintf(text, sizeof text, active ? "%s bids you farewell." : "You dump %s out back.",
                 name);
        bar_message(game, text);
        remove_selected(game, true);
    }
    party_list(game);
    return !vm->abort;
}

/* Alt's Level (7070:1b15): "Novice Squire Veteran Adept Champion" (DS:5241), the
 * difficulty (variable 0x27) becoming the item's number from 1; Escape keeps it. False
 * where the keys ran out. */
static bool alt_level(cok_adventure *game)
{
    static const char *const words[] = {"Novice", "Squire", "Veteran", "Adept", "Champion"};
    log_menu(game, words, 5);
    int choice = bar_menu(game, "", words, 5, false, true, 0, 0);
    if (choice < 0) return false;
    log_choice(game, choice);
    if (choice < 5) cok_ecl_set_var(&game->vm, COK_VAR_DIFFICULTY, (uint16_t)(choice + 1));
    return true;
}

/* Alt's Speed (7070:1bab): "Game Speed (0=fastest 9=slowest) = N" on row 18 (the text
 * in colour 7, unwiped), and the bar's Faster (Down, while above 0), Slower (Up, below 9)
 * and Exit: Escape is Exit, and the other filtered keys do nothing. The speed (0-9) is
 * the variable 0x12 the text's delay reads. The text's row is cleared on Exit (177e:1665).
 * False where the keys ran out. */
static bool alt_speed(cok_adventure *game)
{
    for (;;) {
        unsigned speed = cok_ecl_var(&game->vm, COK_VAR_SPEED);
        char text[64];
        snprintf(text, sizeof text, "Game Speed (0=fastest 9=slowest) = %u", speed);
        say(game, 1, 0x12, text, 7);
        const char *items[3];
        int index[3], n = 0;
        if (speed > 0) {
            index[n] = 0;
            items[n++] = "Faster";
        }
        if (speed < 9) {
            index[n] = 1;
            items[n++] = "Slower";
        }
        index[n] = 2;
        items[n++] = "Exit";
        log_menu(game, items, (size_t)n);
        int choice = bar_menu(game, "game speed: ", items, (size_t)n, true, true, 0, 0);
        if (choice < 0) return false;
        log_choice(game, choice);
        int pick = -1;
        if (choice == 0x1b) {
            pick = 2;
        } else if (choice == 0x84) {
            if (speed > 0) set_speed(game, speed - 1u);
        } else if (choice == 0x88) {
            if (speed < 9) set_speed(game, speed + 1u);
        } else if (choice >= 0x80) {
            continue;
        } else {
            pick = index[choice];
        }
        if (pick == 2) break;
        if (pick == 0 && speed > 0) set_speed(game, speed - 1u);
        if (pick == 1 && speed < 9) set_speed(game, speed + 1u);
    }
    clear_cells(game, 1, 0x12, 0x26, 0x16);
    return true;
}

/* Alt (7070:1ce2), from the camp's menu: its words "Order Drop Speed Icon Level Exit"
 * in the bar, each item below and its routine. Drop only with more than one member
 * (variable 0x20). Icon is the combat icon editor (7070:1da5 calls 2d99:13ff), not
 * ported: it stops for a member, and an NPC has none. Down, End, Home and Up stop
 * (5e22:067c: the list's selection); the other filtered keys do nothing. Escape is Exit.
 * False where the run stopped. */
static bool alt(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (;;) {
        log_menu(game, alt_words, ALT_ITEMS);
        int choice = bar_menu(game, "", alt_words, ALT_ITEMS, true, true, 0, 0);
        if (choice < 0) return false;
        if (choice == 0x1b) choice = ALT_EXIT;
        else if (choice >= 0x80) {
            if (list_key(choice))
                return cok_adventure_not_yet(game, "a member picked by a key in the adventure's "
                                                   "camp's Alt", "dqk 5e22:067c");
            continue;
        }
        log_choice(game, choice);
        bool ok = true;
        switch (choice) {
        case ALT_ORDER: ok = alt_order(game); break;
        case ALT_DROP:
            if (cok_ecl_load_var(vm, 0x20) > 1) ok = alt_drop(game);
            break;
        case ALT_SPEED: ok = alt_speed(game); break;
        case ALT_ICON:
            if (vm->character == NULL) {
                cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                   "Icon with no character selected reads through NULL (7070:1da5)");
                return false;
            }
            if (cok_field_u8(L, vm->character, COK_FIELD_NPC) < 0x80)
                return cok_adventure_not_yet(game, "Icon in the adventure's camp (the combat icon "
                                                   "editor)", "dqk 2d99:13ff");
            break;
        case ALT_LEVEL: ok = alt_level(game); break;
        case ALT_EXIT: return true;
        default: break;
        }
        if (!ok || vm->abort) return false;
    }
}

/* The adventure's camp menu (7070:2d3c), from Encamp and PROGRAM 9: its words
 * "View Magic Rest Alt Fix Load Save Exit" (DS:51d2), by index. Rest and Fix are
 * off where variable 0x2b is 1 and 0x2c is 0x64 or more (2dfd-2e09). */
enum { ENCAMP_VIEW, ENCAMP_MAGIC, ENCAMP_REST, ENCAMP_ALT, ENCAMP_FIX, ENCAMP_LOAD,
       ENCAMP_SAVE, ENCAMP_EXIT, ENCAMP_ITEMS };
static const char *const encamp_words[ENCAMP_ITEMS] = {"View", "Magic", "Rest", "Alt",
                                                       "Fix",  "Load",  "Save", "Exit"};

/* The camp's forget of the scrolls' marks (7070:02f2, at its start and its end, for
 * each member's items: 0218). An item of type 0x27 or 0x28 (the item record's type at
 * 0x28, tested by 7fd8:43f7) has its marks cleared: bit 7 of its spells at 0x36-0x38
 * (02aa-02d5). A container (0x49, a scroll's spells) is walked instead (024c-02a6),
 * clearing its contents' marks; the port holds no contents (604b:05fd drops a scroll
 * that has some, so a kept one's count is 0), so there is none here. Memorized spells
 * are not forgotten (camp_forgets_memorized is false: 7070:02f2 leaves them). */
static void forget_members(cok_adventure *game)
{
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        for (size_t n = 0; n < c->item_count; ++n) {
            uint8_t *item = c->items[n];
            if (item[0x28] != 0x27 && item[0x28] != 0x28) continue;
            for (size_t k = 0x36; k <= 0x38; ++k) item[k] &= 0x7f;
        }
    }
}

/* The adventure's camp menu (7070:2d3c). Returns false where the run stopped.
 * Mode 2 while it runs (DS:ae57; DS:ae67 keeps the last), the camp's screen
 * (encamp_screen), its text on row 18; each item in the order of its words, by
 * the bar's buttons, Escape is Exit. View is the sheet (86b8:0b0c, view_character).
 * Load asks "Game not saved. Load anyway? " where a member is selected and the game
 * is not saved; then it sets DS:acc4 (5f07:0004) and DS:b7a7 and returns with no
 * restore (2f89): encamp then views and marks as usual, the run halts, the adventure's
 * loop loads it, and main takes the camp from main (start). Save saves the game (604b:3395) and asks
 * "Exit to DOS? " (DS:4776), Yes quitting. Alt is alt (7070:1ce2). Magic, Rest and Fix
 * stop (7070:1671, 0aac, 2ad0). Exit puts the mode back, clears the spells' marks
 * (02f2), the status line and rows 18-22 and the bar. */
static bool encamp_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    queen *q = own(game);
    vm->last_mode = vm->mode; /* 2d42 */
    vm->mode = 2;
    forget_members(game); /* 2db5 */
    encamp_screen(game);
    clear_cells(game, 1, 0x11, 0x26, 0x16);
    say(game, 1, 0x12, "the party makes camp...", 7);
    bool rest_off = cok_ecl_load_var(vm, 0x2b) == 1 && cok_ecl_load_var(vm, 0x2c) >= 0x64;
    for (;;) {
        const char *shown[ENCAMP_ITEMS];
        int index[ENCAMP_ITEMS];
        size_t n = 0;
        char line[96] = "";
        for (int k = 0; k < ENCAMP_ITEMS; ++k) {
            if ((k == ENCAMP_REST || k == ENCAMP_FIX) && rest_off) continue;
            index[n] = k;
            shown[n++] = encamp_words[k];
            size_t m = strlen(line);
            snprintf(line + m, sizeof line - m, "%s%s", m > 0 ? " " : "", encamp_words[k]);
        }
        cok_adventure_log(game, "menu", line);
        int choice = bar_menu(game, "", shown, n, true, true, 0, 0);
        if (choice < 0) return false;
        /* A filtered key other than Escape (Up, Down, Home, End, Alt+a letter) picks a
         * member by 5e22:067c, not ported (as commands() stops on it). */
        if (choice >= 0x80)
            return cok_adventure_not_yet(game, "a member picked by a key", "dqk 5e22:067c");
        int k = choice == 0x1b ? ENCAMP_EXIT : choice < (int)n ? index[choice] : -1;
        if (k < 0) continue;
        log_choice(game, k);
        if (k == ENCAMP_EXIT) break;
        switch (k) {
        case ENCAMP_VIEW:
            if (!view_character(game, NULL)) return false;
            encamp_screen(game); /* 86b8:0d97: the screen as it was (177e:1ad1), mode 2 */
            break;
        case ENCAMP_LOAD:
            if (vm->character != NULL && !q->saved) {
                int yes = yes_no(game, "Game not saved. Load anyway? ", game->picture_shown);
                if (yes < 0) return false;
                if (!yes) break;
            }
            /* 7070:2f22: 5f07:0004 sets DS:acc4 (1), 7070:2f2b DS:b7a7, and the menu
             * returns (2f89) with no restore: no forget, no mode, no status. */
            q->load_asked = q->b7a7 = true;
            vm->halt = true;
            return true;
        case ENCAMP_SAVE: {
            if (!save_game(game)) return false;
            int yes = yes_no(game, "Exit to DOS? ", game->picture_shown); /* DS:4776 */
            if (yes < 0) return false;
            if (yes) {
                cok_adventure_quit(game);
                return false;
            }
            break;
        }
        case ENCAMP_MAGIC:
            return cok_adventure_not_yet(game, "Magic in the adventure's camp", "dqk 7070:1671");
        case ENCAMP_REST:
            return cok_adventure_not_yet(game, "Rest in the adventure's camp", "dqk 7070:0aac");
        case ENCAMP_ALT:
            if (!alt(game)) return false;
            break;
        case ENCAMP_FIX:
            return cok_adventure_not_yet(game, "Fix in the adventure's camp", "dqk 7070:2ad0");
        default: break;
        }
        bottom_bar(game); /* 2f53: 1bdb:036c after each item */
    }
    forget_members(game); /* 2f70 */
    vm->mode = vm->last_mode;
    status_line(game);
    clear_cells(game, 1, 0x12, 0x26, 0x16);
    bottom_bar(game);
    return true;
}

/* Encamp (5f07:0b3b, from the adventure's commands at 5f07:122c and from PROGRAM 9
 * at 5f07:0c6d): the camp vector (vectors[2], DS:b298) runs first, then the camp's
 * menu (encamp_menu), then the view (5f07:0b8a, 5e22:0000). Its menu's interrupted
 * rest (the flag at 2d3c's *result, 0b5e: its vectors 4 and 5) is never set by
 * what is ported, so it is not run. */
static cok_ecl_status encamp(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_status status = cok_ecl_run(vm, vm->vectors[2]);
    if (status != COK_ECL_OK || vm->abort) return status;
    if (!encamp_menu(game) || vm->abort) return vm->status;
    view(game);
    if (vm->abort) return vm->status;
    /* 5f07:0b8f: the party's mark where variable 0x22 is 0 or 0x24 is 1; then 0ba6 clears
     * the game saved (DS:ae5b, read by Load's question and the start menu's camp). */
    if (cok_ecl_load_var(vm, 0x22) == 0 || cok_ecl_load_var(vm, 0x24) == 1) marker(game);
    own(game)->saved = false;
    return vm->status;
}

/* PROGRAM 9 (5f07:0c6d): Encamp, with the ECL position (DS:b7f9) kept across it;
 * then, where variable 0x24 is 1 (the overland), 5f07:01ae, not ported. */
static void program_encamp(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint16_t at = vm->ip;
    cok_ecl_status status = encamp(game);
    vm->ip = at;
    if (status != COK_ECL_OK || vm->abort) return;
    if (cok_ecl_load_var(vm, 0x24) == 1)
        cok_adventure_not_yet(game, "PROGRAM 9 on the overland", "dqk 5f07:01ae");
}

/* PROGRAM n (5f07:0baf): Move mode (DS:af3d) is kept and off meanwhile, the
 * selection LOAD CHARACTER changed is restored first (DS:acc5), then n: 0 the
 * camp (camp_program); 1 the ending; 2 the quit (2093:002d, as the camp's
 * Exit ends the game, without its questions); 4-255 (0c8c) sets DS:b7f8,
 * which 6c70:1685 (CALL 1) reads to redraw the view and status, the port's
 * view_changed; 9 Encamp (program_encamp); 3 stops. Move mode is put back after. */
static void program(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    bool moving = game->moving;
    game->moving = false;
    if (vm->restore_character) {
        cok_ecl_restore_selection(vm);
        vm->restore_character = false;
    }
    unsigned n = (uint8_t)cok_ecl_value(vm, 0);
    if (n == 0) {
        camp_program(game);
    } else if (n == 1) {
        ending(game);
    } else if (n == 2) {
        cok_adventure_quit(game);
    } else if (n == 9) {
        program_encamp(game);
        vm->view_changed = true; /* 0c8c, the tail of every case */
    } else if (n == 3) {
        char what[32];
        snprintf(what, sizeof what, "PROGRAM %u", n);
        cok_adventure_not_yet(game, what, program_cite(n));
    } else {
        vm->view_changed = true;
    }
    game->moving = moving;
}

/* TREASURE ITEMS steel gems jewelry n : id... (dark_queen.c's, 6c70:2127): the three header
 * words are added as unsigned words to the pool's coins 4-6 (steel, gems, jewelry: DS:b1db, b1df,
 * b1e3, 213f-216b). With n > 0 the items come next (2188-22a3, from DS:c6de + 17 * id, the id 0xff
 * is 1a83's): not ported, cited, after the coins. */
static void treasure_items(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    for (size_t k = 0; k < 3; ++k) game->pool.coins[4 + k] += cok_ecl_header_value(vm, k);
    if (vm->operands > 0)
        cok_adventure_not_yet(game, "the treasure", "dqk 6c70:218f (TREASURE ITEMS' items: "
                                                   "the item table)");
}

static bool opcode(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    /* The demonstration (5f07:0f46): a key typed is read after each
     * instruction, Escape or '`' ending it (DS:a890, 2093:0006), the
     * others dropped. */
    if (game->demo && cok_adventure_key_pending(game)) {
        input in;
        int key = read_input(game, &in);
        if (key == 0x1b || key == 0x60) {
            vm->halt = true;
            return true;
        }
    }
    switch (cok_ecl_meaning(vm)) {
    case COK_ECL_PRINT: case COK_ECL_PRINTCLEAR: {
        if (vm->operand[0].type < 0x80)
            snprintf(vm->string[0], sizeof vm->string[0], "%u", cok_ecl_value(vm, 0));
        bool clear = cok_ecl_meaning(vm) == COK_ECL_PRINTCLEAR;
        if (clear) vm->cursor = (cok_text_cursor){1, 0x11};
        game->text_shown = true;
        own(game)->text_delay = true; /* 6c70:1d02 */
        print_text(game, vm->string[0], clear);
        own(game)->text_delay = false; /* 6c70:1d84, whatever the keys */
        return true;
    }
    case COK_ECL_PRESS_KEY:
        cok_adventure_log(game, "menu", "~Press button or enter to continue"); /* DS:4281 */
        continue_prompt(game, true, game->picture_shown);
        return true;
    case COK_ECL_JOURNAL: {
        char line[64];
        snprintf(line, sizeof line, "This is recorded as journal entry %u.", /* DS:78a1 */
                 (unsigned)(uint8_t)cok_ecl_value(vm, 0));
        vm->cursor = (cok_text_cursor){1, 0x11};
        own(game)->text_delay = true; /* 6c70:32fc */
        bool shown = print_colour(game, line, true, 7); /* 6c70:32b2: colour 7 whatever 0x39 */
        own(game)->text_delay = false; /* 6c70:3323 */
        if (shown) {
            cok_adventure_log(game, "menu", "~Press button or enter to continue");
            continue_prompt(game, true, game->picture_shown);
        }
        return true;
    }
    case COK_ECL_YES_NO: {
        /* 5f07:13c4: 1bdb:0424, the flags cleared first (2093:0015 with 4: 13c8). */
        static const char *const items[] = {"Yes", "No"};
        for (int k = 0; k < 4; ++k) vm->flags[k] = false;
        log_menu(game, items, 2);
        int yes = yes_no(game, "", game->picture_shown);
        if (yes < 0) return true;
        log_choice(game, yes ? 0 : 1);
        vm->flags[yes ? 0 : 1] = true;
        bottom_bar(game);
        return true;
    }
    case COK_ECL_HORIZONTAL_MENU: horizontal(game, false); return true;
    case COK_ECL_ASK_MENU: horizontal(game, true); return true;
    case COK_ECL_VERTICAL_MENU: vertical(game); return true;
    case COK_ECL_INPUT_NUMBER: input_number(game); return true;
    case COK_ECL_INPUT_STRING: input_string(game); return true;
    case COK_ECL_CLEAR_EXIT:
        clear_cells(game, 1, 0x11, 0x26, 0x16); /* then the shared handler */
        return false;
    case COK_ECL_LOAD_MAP: load_files(game); return true;
    case COK_ECL_PICTURE: picture(game); return true;
    case COK_ECL_CALL:
        if (cok_ecl_call_of(vm->rules, cok_ecl_address(vm, 0)) != COK_ECL_CALL_ANIMATE)
            return false;
        animate(game);
        return true;
    case COK_ECL_PROGRAM: program(game); return true;
    case COK_ECL_TREASURE_ITEMS: treasure_items(game); return true;
    case COK_ECL_ECL_CLOCK: {
        /* 5f07:0a35: the first operand, a byte, is the count and the second,
         * a word, the unit (1396:044d). Its count loop's byte counter wraps at
         * 255 and never ends; a unit past 6 is a word index into its 7-word
         * array (1396:0483), so the original writes its own locals. */
        unsigned count = (uint8_t)cok_ecl_value(vm, 0);
        uint16_t unit = cok_ecl_value(vm, 1);
        if (count == 255) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "ECL CLOCK 255 loops for ever (1396:044d's byte counter wraps)");
        } else if (count > 0 && unit > 6) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "ECL CLOCK unit %u writes past 1396:044d's seven units", unit);
        } else if (count > 0) {
            pass_time(game, count, unit);
        }
        return true;
    }
    case COK_ECL_DUMP: {
        /* 5f07:0c9b, 177e:1e31: a selection that is not in the list is kept (1e31
         * jumps to 1f20 with no unlink, no count out); the shared DUMP selects the
         * first instead. None selected, or in the list, is the shared one's. */
        if (vm->character == NULL ||
            cok_party_index(&game->party, vm->character) < game->party.count)
            return false;
        cok_ecl_keep_selection(vm);
        cok_adventure_party(game);
        return true;
    }
    case COK_ECL_FIND_SPECIAL:
        /* 5f07:0cc5, 177e:1734: with no selection, the walk starts at the
         * far pointer read from 0000:0004 (the NULL record's bytes 4-7), which
         * the port does not model. */
        if (vm->character != NULL) return false;
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "FIND SPECIAL with no character selected reads through 0000:0004 "
                           "(dqk 177e:1734)");
        return true;
    case COK_ECL_MUSIC: {
        /* 5f07:1420: a song (2023:0006), not played yet (DARK-QUEEN.md's
         * step 5). */
        char line[16];
        snprintf(line, sizeof line, "%u", (unsigned)(uint8_t)cok_ecl_value(vm, 0));
        cok_adventure_log(game, "music", line);
        return true;
    }
    default: return false;
    }
}

/* Variable 0x3d: the square's backdrop choice (8a8b:0807). */
static uint16_t value(cok_adventure *game, uint16_t address)
{
    (void)address;
    return cok_view_backdrop_choice(&game->view, game->vm.map_x, game->vm.map_y);
}

/* A store to 0x3a sets the frame's colours from 0 to 6 (6c70:07fd:
 * 20b2:047c, where the value changes; setting them again changes
 * nothing). */
static void stored(cok_adventure *game, uint16_t address, uint16_t value)
{
    if (address == 0x3a && value < 7) frame_palette(game);
}

/* The front end's idle work while the game waits for a key (1bdb:1008's
 * loop): the colours' cycles (261b:05be) and, in a menu that animates,
 * the small picture's next frame each half second. animates says in *ms
 * when the next is due; animate then lets that time pass and does it. */
static bool animates(const cok_adventure *game, uint32_t *ms)
{
    queen *q = game->own;
    if (q == NULL) return false;
    uint64_t due = UINT64_MAX;
    uint32_t next;
    if (cok_vga_cycle_next(&q->palette, &next)) due = ms_until(q, next);
    if (q->menu_picture) {
        uint64_t frame_at = q->frame_from + 500, d = frame_at > q->clock_ms ? frame_at - q->clock_ms : 0;
        if (d < due) due = d;
    }
    if (due == UINT64_MAX) return false;
    if (due < 10) due = 10;
    *ms = (uint32_t)(due > UINT32_MAX ? UINT32_MAX : due);
    q->animate_ms = *ms;
    return true;
}

static void animate_menu(cok_adventure *game)
{
    queen *q = own(game);
    q->clock_ms += q->animate_ms;
    q->animate_ms = 0;
    cok_vga_cycle_colours(&q->palette, ticks(q));
    if (q->menu_picture && q->clock_ms - q->frame_from >= 500) {
        q->frame_from = q->clock_ms;
        next_frame(q);
        picture_frame(game);
    }
}

/* The mouse's cursor, which the game draws itself (22b7:1871, 2aa6:15e3:
 * ALWAYS.TLB's 1, the sword, over a control; its 2, the shield,
 * elsewhere), onto pixels, a copy of the screen, where the pointer is. */
static void cursor(cok_adventure *game, uint8_t *pixels)
{
    queen *q = own(game);
    if (q == NULL || !q->cursor_shown || !game->mouse.driver.installed) return;
    mouse_start(game);
    const hlib_library *always = lib(game, LIB_ALWAYS);
    if (always == NULL) return;
    int x = game->mouse.driver.x / 2, y = game->mouse.driver.y;
    /* 2aa6:1611-1647: the first control holding the pointer gives its
     * cursor (0: the sword); none, the shield. */
    unsigned id = 2;
    for (size_t i = 0; i < q->control_count; ++i)
        if (inside(&q->controls[i], x, y)) {
            id = q->control_cursor[i] != 0 ? q->control_cursor[i] : 1;
            break;
        }
    const uint8_t *record;
    size_t size;
    cok_vga_image image = {0};
    if (hlib_record(always, id, &record, &size) != HLIB_OK ||
        cok_vga_decode(record, size, always, &image) != COK_VGA_OK)
        return;
    /* At the pointer less the hotspot, to the pixel (not to a plane's
     * four, as the game's pictures): the header's row (image.x) is taken
     * from y, the column (image.y) from x. */
    cok_picture copy = game->screen;
    copy.pixels = pixels;
    cok_vga_draw(&copy, &image, x - image.y, y - image.x, NULL, NULL);
    cok_vga_free(&image);
}

/* Startup and the title. */

/* 64b2:0131's part: the font (25a7:04b5 reads GAME.FON, its 0x480 bytes;
 * no font for characters below 0x20 follows), ALWAYS's palette (0x00-0x0f),
 * the frame's (20b2:047c), the screen. */
static bool queen_open(cok_adventure *game)
{
    queen *q = calloc(1, sizeof *q);
    if (q == NULL) {
        snprintf(game->error, sizeof game->error, "out of memory");
        return false;
    }
    game->own = q;
    q->big_id = q->big_last = NO_BIG;
    q->picture_id = 0xff;
    q->state = STATE_VIEW;
    q->cursor_shown = true;
    q->text_delay = true;
    if (cok_picture_create_vga(&game->screen, 40, 200, 1,
                               (const uint8_t (*)[3])q->palette.rgb) != COK_PICTURE_OK) {
        snprintf(game->error, sizeof game->error, "out of memory");
        return false;
    }
    char path[sizeof game->assets + 64];
    cok_adventure_archive_path(game, "GAME.FON", path, sizeof path);
    FILE *f = fopen(path, "rb");
    size_t got = f != NULL ? fread(q->font, 1, sizeof q->font, f) : 0;
    if (f != NULL) fclose(f);
    if (got != sizeof q->font) {
        fail(game, "%.400s: %s", path, "not a font of 96 characters");
        return false;
    }
    const hlib_library *always = lib(game, LIB_ALWAYS);
    if (always == NULL || lib(game, LIB_FRAME) == NULL) return false;
    set_palette(game, always, 0, 0);
    frame_palette(game);
    return true;
}

static void queen_close(cok_adventure *game)
{
    queen *q = own(game);
    if (q == NULL) return;
    for (unsigned i = 0; i < LIBS; ++i) free(q->libs[i].bytes);
    for (unsigned image = 0; image < 2; ++image)
        for (unsigned slot = 0; slot < ICON_SLOTS; ++slot) free(q->icons[image][slot].bytes);
    free(q);
    game->own = NULL;
}

/* Title scene n (6523:00bf): TITLE's backdrop (record 1 for scenes 1-3,
 * 5 for 4-8: its picture 1 and palette), then the scene's library (n + 1,
 * or n + 2 from 4): its picture 1, and 2 for scenes 3, 4, 5, 7 and 8; the
 * colours 0x10-0xff black, then its palette, its cycles with it (the
 * backdrop's stopped by the black). */
static bool title_scene(cok_adventure *game, unsigned n)
{
    hlib_library back, scene;
    if (!sub_library(game, LIB_TITLE, n <= 3 ? 1 : 5, &back) ||
        !sub_library(game, LIB_TITLE, n <= 3 ? n + 1 : n + 2, &scene))
        return false;
    draw(game, &back, 1, 0, 0, NULL, NULL);
    set_palette(game, &back, 0, 0);
    if (n == 6) set_palette(game, &scene, 0, 0);
    draw(game, &scene, 1, 0, 0, NULL, NULL);
    if (n == 3 || n == 4 || n == 5 || n == 7 || n == 8) draw(game, &scene, 2, 0, 0, NULL, NULL);
    black(game, 0x10);
    set_palette(game, &scene, 0, 0);
    return true;
}

/* The title (6523:0345): scenes 1-8, each until a key or its time (800
 * hundredths): Space or the time the next; Escape ends it there; another
 * key, or the last scene's end, ends it with ALWAYS's palette again
 * (6523:0319). False where it failed (the run's
 * status set) or the keys ran out. */
static bool title(cok_adventure *game)
{
    own(game)->cursor_shown = false;
    game->title = true;
    enter_state(game, STATE_TITLE);
    for (unsigned n = 1; n <= 8; ++n) {
        if (!title_scene(game, n)) {
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return false;
        }
        cok_adventure_flush(game);
        int key = timed_wait(game, 800);
        if (key < 0) return false;
        if (key == 0x1b) {
            /* 6523:03a9: the cursor shown, and ALWAYS's colours not set
             * again (6523:0319 passed over). */
            game->title = false;
            own(game)->cursor_shown = true;
            return true;
        }
        if (key != ' ') break;
    }
    game->title = false;
    set_palette(game, lib(game, LIB_ALWAYS), 0, 0); /* 6523:0319 */
    own(game)->cursor_shown = true; /* 22b7:2122 */
    return true;
}

/* Main's menu (5ebd:01e4, again 5ebd:03f9 after a demonstration): the full
 * window, the name and version (5ebd:00b6, DS:4d73, 4d8b), Play, Demo and
 * Quit, which after timeout hundredths with no key gives Demo (DS:c5ce,
 * c5d2: 7000, 0x4b0 after a demonstration); Escape (or '`': 1bdb:06c6's
 * filter, 1206's 0x1b) gives 0x1b, which main takes as Play, as anything
 * but Demo and Quit (5ebd:00ef: DS:ae59 set for Demo alone). Returns it, -1 if
 * the keys ran out. */
static int main_menu(cok_adventure *game, uint16_t timeout)
{
    full_window(game);
    text(game, 40, 64, "The Dark Queen of Krynn", 0x87);
    text(game, 48, 32, "Version 1.1        July 29,1992", 0x87);
    static const char *const items[] = {"Play", "Demo", "Quit"}; /* DS:4ed4 */
    cok_adventure_log(game, "menu", "Play Demo Quit");
    int choice = bar_menu(game, "", items, 3, false, false, timeout, 1);
    if (choice < 0) return -1;
    log_choice(game, choice);
    return choice;
}

/* The camp's screen (20b2:03ce): cells 1-38 by 1-13 cleared, FRAME 1-3,
 * GEN.TLB's picture (its hotspot puts it at rows 110-199) in the frame's
 * colours (GEN's own palette is not set). */
static bool camp_screen(cok_adventure *game)
{
    const hlib_library *gen = lib(game, LIB_GEN);
    if (gen == NULL) return false;
    clear_cells(game, 1, 1, 0x26, 0x0d);
    for (unsigned i = 1; i <= 3; ++i) draw_frame_record(game, i, 0, 0, NULL);
    draw(game, gen, 1, 0, 0, NULL, NULL);
    return true;
}

/* The party's list (5e22:023b, its selected member given): not on the
 * overland (mode 3 with variable 0x24 1) nor under a big picture shown
 * outside it (DS:b256); from column 1 in the camp (mode 0), else 0x11:
 * "Name" there and "AC\tHP" at 0x21 on row 2 in light red (0x0c), then a
 * row a member from 4, each cleared to column 38 first (1a62:09a1): its
 * name (177e:1697: light cyan where selected; black where it cannot act,
 * +0x18b 0; light magenta where it may train, +0xcd; else grey), its AC
 * (177e:0724: 0x3c less +0x18e, a '-' before it where that is below 0,
 * and a blank after, in grey, ending at column 0x22) and its hit points
 * (177e:07b4: +0x198, light cyan below its most, +0x89, else grey, ending
 * at column 0x26); then a row after the list cleared, below row 12. The
 * text's '\t' is a cell of the font's own (its glyph before the font's
 * first, which shows nothing). */
static void party_list(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    if ((vm->mode == 3 && cok_ecl_load_var(vm, 0x24) == 1) || q->big_no_party) return;
    int x0 = vm->mode == 0 ? 1 : 0x11, y = 2;
    text(game, 8 * y, 8 * x0, "Name", 0x8c);
    text(game, 8 * y, 8 * 0x21, "AC HP", 0x8c);
    y += 2;
    for (size_t i = 0; i < game->party.count; ++i, ++y) {
        const uint8_t *c = game->party.members[i]->record;
        clear_cells(game, x0, y, 0x26, y);
        char name[17];
        cok_field_name(L, c, name);
        unsigned fg = c == vm->character ? 0x0b : name_colour(game, c);
        text(game, 8 * y, 8 * x0, name, 0x80 | fg);
        uint8_t ac = cok_field_u8(L, c, COK_FIELD_AC);
        int value = ac - 0x3c;
        char line[16];
        snprintf(line, sizeof line, "%s%d ", value > 0 ? "-" : "", value < 0 ? -value : value);
        int pad = ac > 0x45 ? 0 : ac > 0x32 && ac <= 0x3c ? 2 : 1;
        text(game, 8 * y, 8 * (0x20 + pad), line, 0x87);
        uint8_t hp = cok_field_u8(L, c, COK_FIELD_HIT_POINTS);
        pad = hp > 99 ? 0 : hp > 9 ? 1 : 2;
        snprintf(line, sizeof line, "%u", hp);
        text(game, 8 * y, 8 * (0x24 + pad), line,
             hp < cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS) ? 0x8b : 0x87);
    }
    if (y < 0x0c) clear_cells(game, x0, y, 0x26, y);
}

static void party_hook(cok_adventure *game)
{
    /* 5f07:10cd draws it but in the demonstration (DS:ae59); its other
     * callers there are not reached (block 7 stops at its ADD NPC). */
    if (own(game) != NULL && !game->demo) party_list(game);
}

/* The status (5e22:03f8), but on the overland (mode 3): cells 26-38 by
 * 13-15 cleared; the time on row 15 from variables 8 (the hour), 7 and 6
 * (the minutes' tens and units): "12:mm AM" for hour 0, "h:mm AM" below
 * 12, "12:mm PM" for 12, "h-12:mm PM" (1396:05aa: the minutes with a 0
 * before one digit); the square "x,y" on row 13 unless variable 0x1a is
 * set; the compass's needle by the facing (FRAME 25-28, as 20b2:00e4);
 * on row 14 "camping" in mode 2, else "searching" where variable 0x19's
 * bit 0 is set; all in grey. */
static void status_line(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (own(game) == NULL || vm->mode == 3) return;
    unsigned minutes = cok_ecl_load_var(vm, 7) * 10u + cok_ecl_load_var(vm, 6);
    char mm[8], line[32];
    snprintf(mm, sizeof mm, minutes < 10 ? "0%u" : "%u", minutes & 0xff);
    clear_cells(game, 0x1a, 0x0d, 0x26, 0x0f);
    unsigned hour = cok_ecl_load_var(vm, 8);
    if (hour / 12 == 0) {
        if (hour == 0)
            snprintf(line, sizeof line, "12:%s AM", mm);
        else
            snprintf(line, sizeof line, "%u:%s AM", hour, mm);
    } else {
        snprintf(line, sizeof line, "%u:%s PM", hour == 12 ? 12 : hour - 12, mm);
    }
    text(game, 8 * 0x0f, 8 * 0x1a, line, 0x87);
    if (cok_ecl_load_var(vm, 0x1a) == 0) {
        snprintf(line, sizeof line, "%u,%u", (uint8_t)vm->map_x, (uint8_t)vm->map_y);
        text(game, 8 * 0x0d, 8 * 0x1a, line, 0x87);
    }
    static const uint8_t needle[8] = {25, 25, 28, 26, 26, 26, 27, 25};
    draw_frame_record(game, needle[vm->direction & 7], 0, 0, NULL);
    if (vm->mode == 2)
        text(game, 8 * 0x0e, 8 * 0x1a, "camping", 0x87);
    else if (cok_ecl_load_var(vm, 0x19) & 1)
        text(game, 8 * 0x0e, 8 * 0x1a, "searching", 0x87);
}

/* The record's words and bytes (the sheet's own reads, at the offsets
 * dark_queen_character names). */
static unsigned rec16(const uint8_t *c, size_t at)
{
    return (unsigned)(c[at] | c[at + 1] << 8);
}

/* 1a62:0046's text at cell x, y in fg on the window's grey (bg 0 above
 * row 0x17: 8). */
static void say(cok_adventure *game, int x, int y, const char *s, unsigned fg)
{
    text(game, 8 * y, 8 * x, s, 0x80 | (fg & 15));
}

/* 1a62:09fb: blanks before s until it is width long. */
static void right(char *s, size_t size, size_t width)
{
    size_t n = strlen(s);
    if (n >= width || width >= size) return;
    memmove(s + (width - n), s, n + 1);
    memset(s, ' ', width - n);
}

/* The sheet's tables (DS:3bde, 3bca, 3ba6, 3b86, 3af6, 3c0a, 3c2a,
 * 3bd2), by the record's words. */
static const char *const sheet_status[] = {"Okay", "Animated", "tempgone", "Running",
                                           "Unconscious", "Dying", "Dead", "Petrified", "Gone"};
static const char *const sheet_gender[] = {"Male", "Female", "Steel"};
static const char *const sheet_alignment[] = {
    "Lawful Good", "Lawful Neutral", "Lawful Evil",     "Neutral Good", "True Neutral",
    "Neutral Evil", "Chaotic Good",  "Chaotic Neutral", "Chaotic Evil"};
static const char *const sheet_race[] = {"Silvanesti Elf", "Qualinesti Elf", "Half-Elf",
                                         "Mountain Dwarf", "Hill Dwarf",     "Kender",
                                         "Human",          "Monster"};
static const char *const sheet_class[] = {
    "Cleric", "Knight", "Fighter", "Paladin", "Ranger", "Mage", "Thief", "Monk",
    "Cleric/Fighter", "Cleric/Fighter/Mage", "Cleric/Ranger", "Cleric/Mage", "Cleric/Thief",
    "Fighter/Mage", "Fighter/Thief", "Fighter/Mage/Thief", "Mage/Thief", "Illegal type"};
static const char *const sheet_deity[] = {"None", "Paladine", "Majere", "Kiri-Jolith",
                                          "Mishakal", "Sirrion", "Reorx", "Shinare"};
static const char *const sheet_order[] = {"", "White", "Red", "Black", "gains "};
static const char *const sheet_coins[] = {"Steel", "Gems", "Jewelry"};

/* A word of the record as an index of a table of n, or NULL past it (the
 * original reads on past its table). */
static const char *named(const char *const *table, size_t n, unsigned i)
{
    return i < n ? table[i] : NULL;
}

/* 86b8:096c: score k (0 strength ... 5 charisma) at row 9 + k: cells
 * 5-11 cleared, its value (+0x79 + 2k) from cell 5 (6 below 10) in grey
 * (light cyan where lit); a strength of 18 with an exceptional one
 * (+0x84) "(nn)" after it at cell 7 ("0n" below 10, "00" for 100); a '*'
 * at cell 12 where it is not its base (+0x78 + 2k; the exceptional's
 * +0x85), else a blank. */
static void sheet_score(cok_adventure *game, const uint8_t *c, int k, bool lit)
{
    unsigned fg = lit ? 0x0b : 0x07;
    clear_cells(game, 5, 9 + k, 0x0b, 9 + k);
    uint8_t score = c[0x79 + 2 * k];
    char line[16];
    snprintf(line, sizeof line, "%u", score);
    say(game, score < 10 ? 6 : 5, 9 + k, line, fg);
    if (k == 0 && score == 18 && c[0x84] > 0) {
        if (c[0x84] == 100)
            snprintf(line, sizeof line, "(00)");
        else
            snprintf(line, sizeof line, c[0x84] < 10 ? "(0%u)" : "(%u)", c[0x84]);
        say(game, 7, 9, line, fg);
    }
    bool changed = score != c[0x78 + 2 * k] || (k == 0 && c[0x85] != c[0x84]);
    say(game, 0x0c, 9 + k, changed ? "*" : " ", fg);
}

/* 86b8:0590: the coins (+0x4c, a word each: steel, gems, jewelry): cells
 * 20-38 by rows 9-15 cleared, each kept one's name from row 9 at cell 20 and
 * its count ending at cell 38, in grey. */
static void sheet_coins_box(cok_adventure *game, const uint8_t *c)
{
    char line[16];
    clear_cells(game, 0x14, 9, 0x26, 0x0f);
    for (unsigned k = 0, row = 9; k <= 2; ++k) {
        unsigned coins = rec16(c, 0x4c + 2u * k);
        if (coins == 0) continue;
        say(game, 0x14, (int)row, sheet_coins[k], 0x07);
        snprintf(line, sizeof line, "%u", coins);
        say(game, 0x26 - (int)strlen(line), (int)row, line, 0x07);
        ++row;
    }
}

/* The character sheet (86b8:0000) of the selected member: its window
 * (20b2:00a4); its name (177e:1697) at 1, 1; "Status:"
 * and its status; gender and "%d years", alignment, race; its class, the
 * deity first where it has cleric levels (+0xa5), a mage's order before
 * its "Mage" (+0xaa, 1df9:025d), a knight's order after (+0xa6: " of the
 * Crown", "Sword", "Rose" by +0x19a); "Level" and its levels with '/'
 * between them ("0" for none); "Experience:" and the experience (+0x44, a
 * long) centred in cells 20-39; the scores (86b8:096c); the coins
 * (86b8:0590: each kept, +0x4c, named from row 9 at cell 20, its count
 * ending at cell 38); "Hit Points " and the hit points with their most
 * (177e:07b4); "Armor Class" and the AC (177e:0724); "Encumbrance" (+0x56)
 * and "Movement" (+0x199), "THAC0\t " and its THAC0 (0x3c less +0x18d),
 * "Damage" and the dice (+0x192, +0x194, +0x196); then the readied weapon
 * and armour by their names (177e:03fc) on rows 21 and 22. Its first
 * step, the stats' recompute (177e:0be2, 86b8:065e) and the movement's
 * (177e:0a5f), is queen_sheet's. */
static void sheet_derived(cok_adventure *game, const uint8_t *c);

static void sheet(cok_adventure *game, const cok_character *character)
{
    const uint8_t *c = character->record;
    const cok_character_layout *L = cok_game_rules(game)->character;
    char line[96], name[17];
    /* 20b2:00a4: the full window, FRAME 7 under rows 8, 16 and 20, FRAME
     * 11 (the divider down the middle). */
    full_window(game);
    for (int row = 8; row <= 20; row += row == 16 ? 4 : 8) draw_frame_record(game, 7, 8 * row, 0, NULL);
    draw_frame_record(game, 11, 0, 0, NULL);
    cok_field_name(L, c, name);
    unsigned fg = c[0x18b] == 0 ? 0x00 : c[0xcd] != 0 ? 0x0d : 0x07;
    say(game, 1, 1, name, fg);
    say(game, 0x14, 1, "Status:", 0x0b);
    const char *s = named(sheet_status, 9, rec16(c, 0x64));
    if (s != NULL) say(game, 0x1b, 1, s, 0x07);
    if ((s = named(sheet_gender, 3, rec16(c, 0x60))) != NULL) say(game, 1, 3, s, 0x0b);
    snprintf(line, sizeof line, "%u years", rec16(c, 0x52));
    say(game, 8, 3, line, 0x0b);
    if ((s = named(sheet_alignment, 9, rec16(c, 0x62))) != NULL) say(game, 1, 4, s, 0x0b);
    if ((s = named(sheet_race, 8, rec16(c, 0x58))) != NULL) say(game, 0x14, 4, s, 0x0b);
    s = named(sheet_class, 18, rec16(c, 0x5a));
    snprintf(line, sizeof line, "%s", s != NULL ? s : "");
    if (c[0xa5] > 0) {
        const char *deity = named(sheet_deity, 8, rec16(c, 0x19b));
        char with[160];
        snprintf(with, sizeof with, "%s %s", deity != NULL ? deity : "", line);
        snprintf(line, sizeof line, "%.95s", with);
    }
    if (c[0xaa] > 0) {
        /* The first "M?g" (1df9:025d inserts the order and a blank). */
        for (size_t i = 1; line[i] != '\0' && line[i + 1] != '\0'; ++i) {
            if (line[i - 1] != 'M' || line[i + 1] != 'g') continue;
            const char *order = named(sheet_order, 5, c[0x19d]);
            char with[200];
            snprintf(with, sizeof with, "%.*s%s %s", (int)(i - 1), line,
                     order != NULL ? order : "", line + i - 1);
            snprintf(line, sizeof line, "%.95s", with);
            break;
        }
    }
    if (c[0xa6] > 0) {
        static const char *const knights[] = {" of the Crown", " of the Sword", " of the Rose"};
        if (c[0x19a] >= 1 && c[0x19a] <= 3) {
            size_t n = strlen(line);
            snprintf(line + n, sizeof line - n, "%s", knights[c[0x19a] - 1]);
        }
    }
    say(game, 1, 5, line, 0x0b);
    say(game, 1, 7, "Level", 0x0b);
    line[0] = '\0';
    for (int k = 0; k <= 6; ++k) {
        if (c[0xa5 + k] == 0) continue;
        size_t n = strlen(line);
        snprintf(line + n, sizeof line - n, "%s%u", n > 0 ? "/" : "", c[0xa5 + k]);
    }
    if (line[0] == '\0') snprintf(line, sizeof line, "0");
    say(game, 7, 7, line, 0x0b);
    say(game, 0x18, 6, "Experience:", 0x0b);
    snprintf(line, sizeof line, "%ld",
             (long)(int32_t)(rec16(c, 0x44) | (uint32_t)rec16(c, 0x46) << 16));
    say(game, (0x14 - (int)strlen(line)) / 2 + 0x14, 7, line, 0x07);
    static const char *const scores[] = {"STR ", "INT ", "WIS ", "DEX ", "CON ", "CHA "};
    for (int k = 0; k <= 5; ++k) {
        say(game, 1, 9 + k, scores[k], 0x07);
        sheet_score(game, c, k, false);
    }
    sheet_coins_box(game, c);
    sheet_derived(game, c);
    /* The readied weapon (slot 0, +0xc) and armour (slot 2, +0x14) by
     * their names (177e:03fc) on rows 21 and 22, from cell 1. */
    for (unsigned k = 0; k < 2; ++k) {
        size_t slot = character->slots[k * 2];
        if (slot == 0 || slot > character->item_count) continue;
        queen_item_name(&game->party, character->items[slot - 1], false, line, sizeof line);
        say(game, 1, 0x15 + (int)k, line, 0x07);
    }
}

/* The sheet's lower block (86b8:065e after its recompute): "Hit Points"
 * and the hit points and their most, "Armor Class", "Encumbrance",
 * "THAC0", "Movement" and "Damage" with their values. */
static void sheet_derived(cok_adventure *game, const uint8_t *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    char line[96];
    say(game, 0x14, 3, "Hit Points ", 0x0b);
    clear_cells(game, 0x1f, 3, 0x25, 3);
    uint8_t hp = cok_field_u8(L, c, COK_FIELD_HIT_POINTS), most = c[0x89];
    snprintf(line, sizeof line, "%u", hp);
    say(game, 0x1f, 3, line, hp < most ? 0x0b : 0x07);
    int at = 0x1f + (int)strlen(line);
    snprintf(line, sizeof line, "/%u", most);
    say(game, at, 3, line, 0x07);
    say(game, 1, 0x11, "Armor Class", 0x0b);
    uint8_t ac = c[0x18e];
    int x = (ac >= 1 && ac <= 0x32) || (ac >= 0x3d && ac <= 0x45) ? 0x10
            : ac >= 0x33 && ac <= 0x3c                           ? 0x11
                                                                  : 0x0f;
    clear_cells(game, 0x0f, 0x11, 0x11, 0x11);
    int value = ac - 0x3c;
    snprintf(line, sizeof line, "%s%d ", value > 0 ? "-" : "", value < 0 ? -value : value);
    say(game, x, 0x11, line, 0x07);
    say(game, 0x14, 0x11, "Encumbrance", 0x0b);
    snprintf(line, sizeof line, "%u", rec16(c, 0x56));
    right(line, sizeof line, 5);
    say(game, 0x21, 0x11, line, 0x07);
    say(game, 1, 0x12, "THAC0  ", 0x0b);
    snprintf(line, sizeof line, "%d", 0x3c - c[0x18d]);
    right(line, sizeof line, 2);
    say(game, 0x10, 0x12, line, 0x07);
    say(game, 0x14, 0x12, "Movement", 0x0b);
    snprintf(line, sizeof line, "%u", c[0x199]);
    right(line, sizeof line, 3);
    say(game, 0x23, 0x12, line, 0x07);
    int bonus = (int8_t)c[0x196];
    snprintf(line, sizeof line, "%ud%u", c[0x192], c[0x194]);
    if (bonus > 0) snprintf(line + strlen(line), sizeof line - strlen(line), "+%d", bonus);
    if (bonus < 0) snprintf(line + strlen(line), sizeof line - strlen(line), "-%d", -bonus);
    say(game, 1, 0x13, "Damage", 0x0b);
    right(line, sizeof line, 7);
    say(game, 0x0b, 0x13, line, 0x07);
}

/* What queen_view.c uses (queen_view.h). */

static void bar_message(cok_adventure *game, const char *s);

int queen_list_menu(cok_adventure *game, const char *prompt, const char *const *bar,
                    size_t bar_count, const char *const *items, size_t count, size_t *at,
                    int x1, int y1, int y2, int x2)
{
    return list_menu(game, prompt, bar, bar_count, items, count, at, x1, y1, y2, x2);
}

void queen_bar_message(cok_adventure *game, const char *s)
{
    bar_message(game, s);
}

bool queen_print_window(cok_adventure *game, const char *s, int x1, int y1, int x2, int y2,
                        unsigned fg, bool clear)
{
    return print_window(game, s, x1, y1, x2, y2, fg, clear);
}

int queen_ask(cok_adventure *game, const char *prompt, bool anim)
{
    return yes_no(game, prompt, anim);
}

/* 177e:1697: the character's name at cell x, y: not drawn dark where it
 * cannot act (+0x18b 0: colour 0), 2 where hostile in combat, light
 * magenta where it may train (+0xcd), else grey; "'s" after it where
 * asked. */
void queen_name_line(cok_adventure *game, const cok_character *character, int x, int y,
                     bool possessive)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *c = character->record;
    unsigned fg = c[0x18b] == 0 ? 0
                  : (cok_field_u8(L, c, COK_FIELD_HOSTILE) == 1 && game->vm.mode == 5) ? 2
                  : c[0xcd] != 0 ? 0x0d
                                 : 7;
    char name[17];
    cok_field_name(L, c, name);
    say(game, x, y, name, fg);
    if (possessive) say(game, x + (int)strlen(name), y, "'s", fg);
}

void queen_sheet_coins(cok_adventure *game, const cok_character *character)
{
    sheet_coins_box(game, character->record);
}

void queen_title_window(cok_adventure *game)
{
    full_window(game);
    draw_frame_record(game, 7, 8 * 2, 0, NULL); /* 20b2:000d(2) */
}

/* The party's list with a member given as the selected (5e22:023b's
 * argument): the same as party_list's, which draws vm->character's. */
void queen_party_list(cok_adventure *game, const uint8_t *selected)
{
    cok_ecl *vm = &game->vm;
    uint8_t *saved = vm->character;
    vm->character = (uint8_t *)selected;
    party_list(game);
    vm->character = saved;
}

/* View (86b8:0b0c): the sheet, then its menu (1e29 with DS:4f82's "Items
 * Spells Trade Deposit Drop Lay Cure Exit", those listed: Items with
 * items, Spells with a spell memorized (+0xce-+0x156), Trade with coins
 * but in combat (mode 5) where an NPC that can act and is not hostile
 * may not, Deposit with coins in the vault (mode 10), Drop with coins
 * elsewhere, Lay and Cure for a paladin who may (86b8:314e, 31af: +0x5a 3,
 * not in combat, status 0, Lay without effect 0x8c, Cure with +0x88 left;
 * a former paladin's, 177e:21bd, not ported outside a battle (in one 314e and 31af give 0
 * whatever 21bd says)), Exit always); Exit or Escape leaves it; the others are not ported yet
 * (the run stops).
 * done (NULL outside a battle) is set where Items ends the turn (queen_view_items), and the
 * sheet is then not drawn again. False where the run stopped. */
static bool view_character(cok_adventure *game, bool *done)
{
    cok_ecl *vm = &game->vm;
    const uint8_t *c = vm->character;
    if (c == NULL) return true;
    size_t i = cok_party_index(&game->party, c);
    if (i >= game->party.count) return true;
    cok_character *member = game->party.members[i];
    queen_view_start(member);
    if (!queen_sheet(game, member)) return false;
    for (;;) {
        /* The menu is built again after each of its choices (86b8:0b49). */
        bool spells = false, coins = false;
        for (size_t k = 0; k <= 0x88; ++k)
            if (c[0xce + k] & 0x7f) spells = true;
        for (size_t k = 0; k <= 2; ++k)
            if (rec16(c, 0x4c + 2 * k) > 0) coins = true;
        bool items = member->item_count > 0;
        bool paladin = rec16(c, 0x5a) == 3;
        /* In a battle (mode 5) 21bd does not matter: 314e and 31af give 0 there either way. */
        if (!paladin && c[0xaf] > 0 && vm->mode != 5) {
            cok_adventure_not_yet(game, "a former paladin's Lay and Cure", "dqk 177e:21bd");
            return false;
        }
        bool may = paladin && vm->mode != 5 && rec16(c, 0x64) == 0;
        bool lay = may, cure = may && c[0x88] > 0;
        if (lay)
            for (const cok_effect *e = member->effects; e != NULL; e = e->next)
                if (e->id == 0x8c) lay = false;
        bool on[8] = {items, spells,
                      (c[0x9b] < 0x80 || c[0x18b] == 0 || rec16(c, 0x64) == 1) && coins &&
                          vm->mode != 5,
                      coins && vm->mode == 10, coins && vm->mode != 10, lay && vm->mode != 10,
                      cure && vm->mode != 10, true};
        static const char *const all[8] = {"Items", "Spells", "Trade", "Deposit",
                                           "Drop",  "Lay",    "Cure",  "Exit"};
        const char *shown[8];
        int index[8];
        size_t n = 0;
        char line[96] = "";
        for (int k = 0; k < 8; ++k)
            if (on[k]) {
                index[n] = k;
                shown[n++] = all[k];
                size_t m = strlen(line);
                snprintf(line + m, sizeof line - m, "%s%s", m > 0 ? " " : "", all[k]);
            }
        cok_adventure_log(game, "menu", line);
        int choice = bar_menu(game, "", shown, n, false, false, 0, 0);
        if (choice < 0) return false;
        int k = choice == 0x1b ? 7 : choice < (int)n ? index[choice] : -1;
        if (k < 0) continue;
        log_choice(game, k);
        if (k == 7) return true;
        if (k == 0) {
            if (!queen_view_items(game, member, done)) return false;
            if (done != NULL && *done) return true; /* 0b0c's d66 and d79: no sheet again */
            if (!queen_sheet(game, member)) return false;
            continue;
        }
        if (k == 2) {
            if (!queen_view_trade(game, member)) return false;
            if (!queen_sheet(game, member)) return false;
            continue;
        }
        if (k == 3) {
            cok_adventure_not_yet(game, "coins deposited in the vault", "dqk 86b8:2c32");
            return false;
        }
        if (k == 4) {
            if (!queen_view_drop(game, member)) return false;
            queen_sheet_coins(game, member);
            continue;
        }
        static const char *const routines[] = {"dqk 86b8:0f41", "dqk 849a:2080",
                                               "dqk 86b8:286a", "dqk 86b8:2c32",
                                               "dqk 86b8:2c32", "dqk 86b8:3200",
                                               "dqk 86b8:3319"};
        char what[32];
        snprintf(what, sizeof what, "View's %s", all[k]);
        cok_adventure_not_yet(game, what, routines[k]);
        return false;
    }
}

/* View in a battle (dqk 5045:0af8's 86b8:0b0c(&done), then 86b8:0d8b-0d97). The sheet and its menu
 * are view_character's: Items, Drop and Exit in combat (no Trade), the Use of Items a stop. done is
 * set by Items where a readied item of 0x8c is held (86b8:16bc: the turn is over, no sheet, no
 * arena again); Use is the other writer in a battle, a stop here (86b8:2175). The mode 5 tail
 * is 86b8:0d92's 5837:00e4 (19a4:0630, DS:c5d4: the combat icons' page and palette made current
 * again), not ported: combat_screen says why; then 177e:1ad1
 * (screen_as_it_was), which draws nothing in mode 5. False where the run stopped; true, with
 * vm.abort set, where the keys ran out in the sheet (the turn is not ended and the tail is not
 * run). */
bool queen_view_combat(cok_adventure *game, bool *done)
{
    *done = false;
    if (!view_character(game, done)) return game->vm.abort && game->vm.status == COK_ECL_OK;
    return screen_as_it_was(game);
}

/* The save path (604b:0073 and 000e with DS:ace0 0, DQK.CFG's: the
 * game's SAVE folder): --saves where given, else the assets' SAVE, which
 * the port only reads. */
static void save_path(const cok_adventure *game, char *out, size_t size)
{
    if (game->save_dir[0] != '\0')
        snprintf(out, size, "%s", game->save_dir);
    else
        snprintf(out, size, "%s/SAVE", game->assets);
}

static bool writable(cok_adventure *game, const char *what)
{
    if (game->save_dir[0] != '\0') return true;
    cok_adventure_log(game, "error", what);
    return false;
}

/* The file of name in dir, in any case, into out; false if there is
 * none (DOS's names are of either case). */
static bool find_file(const char *dir, const char *name, char *out, size_t size)
{
    DIR *d = opendir(dir);
    if (d == NULL) return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d)) != NULL)
        if (strcasecmp(e->d_name, name) == 0) {
            snprintf(out, size, "%s/%s", dir, e->d_name);
            found = true;
        }
    closedir(d);
    return found;
}

/* A character's file name (5e98:0044): each of ' =+<>"[].*,?/\:;|'
 * taken out, 8 characters kept, upper-cased (10d7:25fb). */
static void file_name(const char *name, char out[9])
{
    size_t n = 0;
    for (; *name != '\0' && n < 8; ++name)
        if (strchr(" =+<>\"[].*,?/\\:;|", *name) == NULL)
            out[n++] = (char)(*name >= 'a' && *name <= 'z' ? *name - 0x20 : *name);
    out[n] = '\0';
}

/* 64b2:04ec, a new game: the variables 0x0001-0x0400 cleared, then 0x27
 * 3, 0x22 1 (3D), 0x16 0, 0x39 0, 0x12 4 (the speed), 0x20 0 (the party's
 * size); the square 7, 13 facing 2; the wall set (DS:ca03) and the vault
 * (DS:c3ea 'Z') none; no party nor selection (the list dropped, not
 * freed: its callers emptied it); the mode 4, the last 0; the game not
 * saved (DS:ae5b) nor loaded (DS:b1ed: the next block clears its
 * locals, 6c70:0001). */
static void new_game(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    for (uint16_t a = 1; a <= 0x400; ++a) cok_ecl_store(vm, a, 0);
    cok_ecl_store(vm, 0x27, 3);
    cok_ecl_store(vm, 0x22, 1);
    cok_ecl_store(vm, 0x12, 4);
    own(game)->text_delay = true; /* 6478:0916 */
    vm->map_x = 7;
    vm->map_y = 13;
    vm->direction = 2;
    game->wall_ids[0] = 0;
    game->vault_letter = 'Z';
    cok_party_free(&game->party);
    vm->character = NULL;
    vm->mode = 4;
    vm->last_mode = 0;
    own(game)->saved = false;
    own(game)->big_no_party = false;
    own(game)->loaded_game = false; /* DS:b1ed */
    vm->keep_vars = false;
}

/* The saved games' letters A-J there are in dir (604b:2e87: findfirst of
 * stem with any letter and extension, its 7th character A-J), in order,
 * into letters; their count. */
static size_t saved_letters(const char *dir, const char *stem, const char *extension,
                            char letters[10])
{
    bool there[10] = {false};
    DIR *d = opendir(dir);
    if (d != NULL) {
        struct dirent *e;
        size_t n = strlen(stem);
        while ((e = readdir(d)) != NULL) {
            const char *name = e->d_name;
            if (strlen(name) != n + 2 + strlen(extension) || strncasecmp(name, stem, n) != 0 ||
                name[n + 1] != '.' || strcasecmp(name + n + 2, extension) != 0)
                continue;
            int c = name[n] >= 'a' && name[n] <= 'z' ? name[n] - 0x20 : name[n];
            if (c >= 'A' && c <= 'J') there[c - 'A'] = true;
        }
        closedir(d);
    }
    size_t count = 0;
    for (size_t i = 0; i < 10; ++i)
        if (there[i]) letters[count++] = (char)('A' + i);
    return count;
}

/* 1bdb:1e29 with DS:af15's list (604b:2e87): only the items listed are
 * buttons; returns the index in all of them, a filtered key, or -1. */
static int letter_menu(cok_adventure *game, const char *prompt, const char *letters,
                       size_t count)
{
    const char *items[10];
    char one[10][2];
    for (size_t i = 0; i < count; ++i) {
        one[i][0] = letters[i];
        one[i][1] = '\0';
        items[i] = one[i];
    }
    char line[11];
    snprintf(line, sizeof line, "%.*s", (int)count, letters);
    cok_adventure_log(game, "menu", line);
    int choice;
    do {
        choice = bar_menu(game, prompt, items, count, false, false, 0, 0);
        if (choice < 0) return -1;
    } while ((size_t)choice >= count && choice != 0x1b);
    if (choice == 0x1b) return 0x1b;
    return letters[choice] - 'A';
}

/* What a load does after the file (604b:2ab4): the frame's colours where
 * variable 0x3a changed; on the overland (variables 0x22 0, 0x24 1) its
 * big picture loaded (63ae:0321: variable 0x3e's), else the map again
 * (8a8b:12e3, variable 0x13's) where a wall set was loaded (DS:ca03 above
 * 0). The members' combat icons come first, each in list order (member_icon). */
static bool loaded(cok_adventure *game, uint8_t frame_was)
{
    cok_ecl *vm = &game->vm;
    for (size_t i = 0; i < game->party.count; ++i)
        if (!member_icon(game, game->party.members[i]->record)) return false;
    if (cok_ecl_load_var(vm, 0x22) == 0 && cok_ecl_load_var(vm, 0x24) == 1) {
        queen *q = own(game);
        unsigned n = cok_ecl_load_var(vm, 0x3e);
        if (q->big_id != n) {
            if (!sub_library(game, LIB_BIGPIC, n, &q->big)) return false;
            q->big_id = q->big_last = n;
        }
    } else if (game->wall_ids[0] > 0) {
        uint8_t map = (uint8_t)cok_ecl_load_var(vm, 0x13);
        size_t size;
        uint8_t *data = cok_adventure_record(game, cok_game_rules(game)->archives.geo, vm->file,
                                             map, &size);
        if (data == NULL) return false;
        bool ok = cok_view_set_geo(&game->view, data, size);
        free(data);
        if (!ok) {
            snprintf(game->error, sizeof game->error,
                     "GEO.GLB record %u: Unable to load geo in Load3DMap", map);
            return false;
        }
        own(game)->wall_loaded[0] = own(game)->wall_loaded[1] = false;
    }
    if (cok_ecl_load_var(vm, 0x3a) != frame_was) frame_palette(game);
    return true;
}

/* A Death Knights saved game imported (604b:2bcf, Dkk's letter in dir): a
 * new game's state (64b2:04ec); its party's size, the byte at Death
 * Knights' 0xa18 (604b:2b8a: none read, 0; at most 8); then
 * CHRDAT<letter><n>.GAM for n from 1 while there is one, each imported
 * (604b:0b82: cok_character_import, whose stats recompute 177e:0be2 the
 * import runs; the levels, 8bcc:07f1, 604b:2bcf does not) and joined
 * unless it is an NPC; the first selected. False where the
 * run stopped. */
static bool import_game(cok_adventure *game, const char *dir, char letter)
{
    cok_ecl *vm = &game->vm;
    const cok_rules *rules = cok_game_rules(game);
    const cok_rules_import *I = rules->characters.import;
    const cok_character_layout *L = rules->character;
    new_game(game);
    char name[64], path[700];
    snprintf(name, sizeof name, "%s%c.%s", I->from->saves.game, letter,
             I->from->saves.game_extension);
    unsigned count = 0;
    if (find_file(dir, name, path, sizeof path)) {
        FILE *f = fopen(path, "rb");
        if (f != NULL) {
            if (fseek(f, (long)cok_saved_game_count_offset(I->from), SEEK_SET) == 0) {
                int c = fgetc(f);
                if (c != EOF) count = (unsigned)c;
            }
            fclose(f);
        }
    }
    if (count > 8) count = 8;
    for (unsigned n = 1; n <= count; ++n) {
        char base[16], file[32];
        snprintf(base, sizeof base, "%s%c%u", I->from->saves.character, letter, n);
        snprintf(file, sizeof file, "%s.%s", base, I->from->saves.record);
        char found[700];
        if (!find_file(dir, file, found, sizeof found)) break;
        char *slash = strrchr(found, '/');
        char *dot = strrchr(found, '.');
        if (slash == NULL || dot == NULL) break;
        *dot = '\0';
        cok_character *c = malloc(sizeof *c);
        if (c == NULL ||
            !cok_character_import(c, rules, dir, slash + 1, dot + 1, &game->item_types,
                                  game->error, sizeof game->error)) {
            free(c);
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return false;
        }
        if (cok_field_u8(L, c->record, COK_FIELD_NPC) >= 0x80 ||
            !cok_party_add(&game->party, rules, c)) {
            cok_character_free(c);
            free(c);
            continue;
        }
        cok_ecl_add_var(vm, COK_VAR_PARTY_SIZE, 1);
    }
    for (size_t i = 0; i < game->party.count; ++i)
        if (!member_icon(game, game->party.members[i]->record)) {
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return false;
        }
    vm->character = cok_party_record(&game->party, 0);
    bottom_bar(game);
    vm->mode = 0;
    return true;
}

/* A character of the roster: its file's base and name, and the list's
 * text ("* %s" for a member, DS:7eb1). */
typedef struct {
    char base[16], name[20], text[24];
} queen_roster;
enum { ROSTER_MOST = 64 };

static int by_name(const void *a, const void *b)
{
    return strcmp(((const queen_roster *)a)->name, ((const queen_roster *)b)->name);
}

/* The roster (604b:054a, 028a): with other (DS:b7ac 4) Death Knights'
 * characters, the .PC files of --import (DQK.CFG's path), else the save
 * path's .QCH, each named by its record's name (604b:01af: 16 bytes at
 * +0x68; Death Knights' a length-prefixed string at +0), by name; their
 * count, into out. */
static size_t roster(cok_adventure *game, bool other, queen_roster *out, size_t most)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_rules_import *I = rules->characters.import;
    const char *extension = other ? I->from->saves.roster : rules->saves.roster;
    char dir[600];
    if (other)
        snprintf(dir, sizeof dir, "%s", game->import_dir);
    else
        save_path(game, dir, sizeof dir);
    size_t count = 0;
    DIR *d = dir[0] != '\0' ? opendir(dir) : NULL;
    if (d == NULL) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < most) {
        const char *dot = strrchr(e->d_name, '.');
        if (dot == NULL || dot == e->d_name || dot - e->d_name > 8 ||
            strcasecmp(dot + 1, extension) != 0)
            continue;
        char path[900];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (f == NULL) continue;
        uint8_t head[0x78] = {0};
        size_t got = fread(head, 1, sizeof head, f);
        fclose(f);
        queen_roster *r = &out[count];
        memset(r, 0, sizeof *r);
        snprintf(r->base, sizeof r->base, "%.*s", (int)(dot - e->d_name), e->d_name);
        if (other) {
            size_t n = got > 0 ? head[0] : 0;
            if (n > 15) n = 15;
            memcpy(r->name, head + 1, n);
        } else if (got >= 0x78) {
            memcpy(r->name, head + 0x68, 15);
        } else {
            continue;
        }
        ++count;
    }
    closedir(d);
    qsort(out, count, sizeof *out, by_name);
    return count;
}

/* 7368:088f: whether a member has that name. */
static bool in_party(cok_adventure *game, const char *name)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (size_t i = 0; i < game->party.count; ++i) {
        char n[17];
        cok_field_name(L, game->party.members[i]->record, n);
        if (strcmp(n, name) == 0) return true;
    }
    return false;
}

/* The speed's wait (1a62:0960: DS:1cd8's time for variable 0x12): pass for it. */
static void speed_pass(cok_adventure *game)
{
    static const unsigned ms[] = {10, 100, 250, 500, 1000, 1500, 2000, 3000, 4000, 5000};
    unsigned speed = cok_ecl_load_var(&game->vm, 0x12);
    pass(game, ms[speed < 10 ? speed : 9]);
}

/* A message on the bar for the speed's time (1a62:0960: DS:1cd8's,
 * variable 0x12's): boxed from cell x + 1 in fg on bg, the bar again. */
static void message_at(cok_adventure *game, const char *s, int x, unsigned fg, unsigned bg)
{
    bottom_bar(game);
    cok_adventure_log(game, "message", s);
    prompt_box(game, x, s, fg, bg);
    speed_pass(game);
    bottom_bar(game);
}

/* 177e:153c: s in the bar (white on grey, 1a62:0046 at row 24, x 0: the
 * box from cell 1, its edge at pixel 7) for the speed's time, in fg (the
 * trainer's 1a62:0904 in 0x0b, x 0 too). */
static void bar_message_colour(cok_adventure *game, const char *s, unsigned fg)
{
    message_at(game, s, 0, fg, 8);
}

static void bar_message(cok_adventure *game, const char *s)
{
    bar_message_colour(game, s, 0x0f);
}

/* A text said in the battle by c in the window at row (177e:156e in mode 5, the say's branch
 * for the battle; the camp's is member_say's): the window cleared from row, c's name on it
 * (177e:1697, light or dark by its state), the text under it in grey from row + 1 (1a62:0311,
 * clear on: its bp+0x12 is 1, so the cursor is homed at x 0x17), then with wait the speed's time (1a62:0960) and the window cleared again from
 * row 10 (177e:1665). The text is logged as the say's are (the name, then the text). c NULL:
 * no name. */
static bool arena_say(cok_adventure *game, cok_character *c, const char *text, uint8_t row,
                      bool wait)
{
    clear_cells(game, 0x17, row, 0x26, 0x15);
    if (c != NULL) {
        char name[17];
        cok_field_name(cok_game_rules(game)->character, c->record, name);
        queen_name_line(game, c, 0x17, row, false);
        cok_adventure_log(game, "print", name);
    }
    cok_adventure_log(game, "print", text);
    if (!print_window(game, text, 0x17, row + 1, 0x26, 0x15, 7, true)) return false;
    if (!wait) return true;
    speed_pass(game);
    clear_cells(game, 0x17, 0x0a, 0x26, 0x15);
    return true;
}

/* A flash on c for a text in the battle (2d99:0f09 -> 51c2:0731, mode 5: kind 1 is an effect's, the
 * four-frame missile, its sound and passes, the draws of each frame), not ported: its own part. A
 * kind 0 flash (a burst) is not checked against the original: it stops under the same citation. */
static bool arena_flash(cok_adventure *game, cok_character *c, uint8_t kind, const char *text)
{
    (void)c;
    (void)text;
    return cok_adventure_not_yet(game, kind != 0 ? "an effect's flash" : "a flash",
                                 "dqk 51c2:0731 (2d99:0f09)");
}

/* 177e:1665 in the battle: cells 0x17-0x26 by 0x0a-0x15 in the window's grey. */
static void arena_clear_text(cok_adventure *game)
{
    clear_cells(game, 0x17, 0x0a, 0x26, 0x15);
}

/* 1a62:0046 (x, y, fg, bg, text). A background of 0 is the grey (8), and on row 23 or below a
 * fg of 7 or 8 is white (15). Row 24 is boxed on the bar (prompt_box); above it the text is drawn
 * in attribute bg * 16 + fg (2102:03a0, one cell 8 by 8). Row 23 in the battle takes another branch
 * (1a62:01d0), not ported. */
static bool arena_text(cok_adventure *game, int x, int y, const char *s, uint8_t fg, uint8_t bg)
{
    if (bg == 0) {
        bg = 8;
        if (y >= 0x17 && (fg == 7 || fg == 8)) fg = 0x0f;
    }
    if (y == 0x18) {
        prompt_box(game, x, s, fg, bg);
        return true;
    }
    if (game->vm.mode == 5 && y == 0x17)
        return cok_adventure_not_yet(game, "a text on row 23 of the battle", "dqk 1a62:01d0");
    text(game, 8 * y, 8 * x, s, (unsigned)(bg & 15) << 4 | (unsigned)(fg & 15));
    return true;
}

/* 1a62:0949: the wait of variable 0x12 times 100 ms (its byte; one draw unless it is 0). */
static void battle_pause(cok_adventure *game)
{
    pass(game, 100u * (uint8_t)cok_ecl_load_var(&game->vm, 0x12));
}

/* Add Character (7368:08dc): "add from where? " Tdqk, Dkk or Exit; the
 * roster (none: "No characters to load.", DS:5f76); then "add a
 * character: " over its list ("Add Exit", DS:501a), each member's "* "
 * first: Add (or Return) reads the current one but a member (604b:2198:
 * its .QCH, or Death Knights' imported, 604b:0b82; then the levels,
 * 8bcc:07f1, 604b:2021: cok_character_levels) and joins it (604b:3491) where fewer than six
 * others (eight with an NPC) are in and, a ranger (+0xa9), fewer than
 * three rangers ("too many rangers in party", DS:7eb6); until Exit (or
 * Escape) or six. The game is then not saved. False where the run
 * stopped. */
static bool add_character(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    full_window(game); /* 1a62:09a1's window */
    static const char *const where[] = {"Tdqk", "Dkk", "Exit"}; /* DS:500c */
    cok_adventure_log(game, "menu", "Tdqk Dkk Exit");
    int from = bar_menu(game, "add from where? ", where, 3, false, false, 0, 0);
    if (from < 0) return false;
    if (from == 0x1b) from = 2;
    log_choice(game, from);
    if (from == 2) return true;
    bool other = from == 1;
    static queen_roster list[ROSTER_MOST];
    size_t count = roster(game, other, list, ROSTER_MOST);
    if (count == 0) {
        bar_message(game, "No characters to load.");
        return true;
    }
    static const char *const bar[] = {"Add", "Exit"}; /* DS:501a */
    size_t at = 0;
    for (;;) {
        const char *items[ROSTER_MOST];
        for (size_t i = 0; i < count; ++i) {
            if (in_party(game, list[i].name))
                snprintf(list[i].text, sizeof list[i].text, "* %s", list[i].name);
            else
                snprintf(list[i].text, sizeof list[i].text, "%s", list[i].name);
            items[i] = list[i].text;
        }
        cok_adventure_log(game, "list", "add a character:");
        int choice = list_menu(game, "add a character: ", bar, 2, items, count, &at, 1, 2, 0x16,
                               0x26);
        if (choice < 0) return false;
        if (choice == 0x1b) choice = 1;
        log_choice(game, choice);
        if (choice == 0 && list[at].text[0] != '*') {
            char dir[600];
            if (other)
                snprintf(dir, sizeof dir, "%s", game->import_dir);
            else
                save_path(game, dir, sizeof dir);
            cok_character *c = malloc(sizeof *c);
            const char *extension =
                other ? rules->characters.import->from->saves.roster : rules->saves.roster;
            char found[700], file[32];
            snprintf(file, sizeof file, "%s.%s", list[at].base, extension);
            bool ok = c != NULL && find_file(dir, file, found, sizeof found);
            if (ok) {
                char *slash = strrchr(found, '/'), *dot = strrchr(found, '.');
                *dot = '\0';
                ok = other ? cok_character_import(c, rules, dir, slash + 1, dot + 1,
                                                  &game->item_types, game->error,
                                                  sizeof game->error)
                           : cok_character_read_file(c, rules, dir, slash + 1, dot + 1,
                                                     &game->item_types, game->error,
                                                     sizeof game->error);
            }
            /* 604b:2021: the stats (0be2) were run as it was read; the
             * levels (8bcc:07f1) follow. */
            if (ok &&
                !cok_character_levels(c, rules, &game->item_types, game->error,
                                      sizeof game->error))
                ok = false;
            if (!ok) {
                free(c);
                cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
                return false;
            }
            if (c->contents_dropped) cok_adventure_log(game, "print", "SCROLLS DROPPED!");
            size_t others = 0, rangers = 0;
            for (size_t i = 0; i < game->party.count; ++i) {
                const uint8_t *m = game->party.members[i]->record;
                if (cok_field_u8(L, m, COK_FIELD_NPC) < 0x80) ++others;
                if (m[0xa9] != 0) ++rangers;
            }
            bool npc = cok_field_u8(L, c->record, COK_FIELD_NPC) >= 0x80;
            bool ranger = c->record[0xa9] != 0;
            if (((!npc && others < 6) || (npc && cok_ecl_load_var(vm, 0x20) < 8)) &&
                (!ranger || rangers < 3) && cok_party_add(&game->party, rules, c)) {
                cok_ecl_add_var(vm, COK_VAR_PARTY_SIZE, 1);
                if (!queen_added_icons(game, c->record)) {
                    cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
                    return false;
                }
                vm->character = c->record; /* 604b:3491: DS:ad67 */
            } else {
                if (ranger && rangers > 2) bar_message(game, "too many rangers in party");
                cok_character_free(c);
                free(c);
            }
        }
        size_t others = 0;
        for (size_t i = 0; i < game->party.count; ++i)
            if (cok_field_u8(L, game->party.members[i]->record, COK_FIELD_NPC) < 0x80) ++others;
        if (choice == 1 || others >= 6 || cok_ecl_load_var(vm, 0x20) >= 8) return true;
    }
}

/* Delete Character (7368:0c23): the save path's roster (none: "No
 * characters to delete.", DS:5f5d), "delete a character: " over its list
 * ("Delete Exit", DS:5023), members marked; Delete (or Return) asks
 * "Delete <name> forever? " (DS:7ed0) and "Are you sure? " (7ee4), then
 * erases its file (217e:01e3) from the save path and the list; until Exit
 * (or Escape) or none left. False where the run stopped. */
static bool delete_character(cok_adventure *game)
{
    full_window(game);
    static queen_roster list[ROSTER_MOST];
    size_t count = roster(game, false, list, ROSTER_MOST);
    if (count == 0) {
        bar_message(game, "No characters to delete.");
        return true;
    }
    static const char *const bar[] = {"Delete", "Exit"}; /* DS:5023 */
    size_t at = 0;
    for (;;) {
        const char *items[ROSTER_MOST];
        for (size_t i = 0; i < count; ++i) {
            if (in_party(game, list[i].name))
                snprintf(list[i].text, sizeof list[i].text, "* %s", list[i].name);
            else
                snprintf(list[i].text, sizeof list[i].text, "%s", list[i].name);
            items[i] = list[i].text;
        }
        cok_adventure_log(game, "list", "delete a character:");
        int choice = list_menu(game, "delete a character: ", bar, 2, items, count, &at, 1, 2,
                               0x16, 0x26);
        if (choice < 0) return false;
        if (choice == 0x1b) choice = 1;
        log_choice(game, choice);
        if (choice == 0) {
            char line[64];
            snprintf(line, sizeof line, "Delete %s forever? ", list[at].name);
            int yes = yes_no(game, line, false);
            if (yes < 0) return false;
            if (yes) yes = yes_no(game, "Are you sure? ", false);
            if (yes < 0) return false;
            if (yes) {
                if (!writable(game, "Delete: no save path (--saves)")) return true;
                char dir[600], file[32], found[700];
                save_path(game, dir, sizeof dir);
                snprintf(file, sizeof file, "%s.%s", list[at].base,
                         cok_game_rules(game)->saves.roster);
                if (find_file(dir, file, found, sizeof found)) remove(found);
                cok_adventure_log(game, "delete", file);
                memmove(&list[at], &list[at + 1], (count - at - 1) * sizeof *list);
                --count;
                if (at >= count && at > 0) --at;
            }
        }
        if (choice == 1 || count == 0) return true;
    }
}

/* The saved game at path read as Load Saved Game reads it (604b:2e87's tail,
 * DS:b1ed set first): the game's read (cok_adventure_restore), the view's
 * pictures and walls as loaded (loaded), the bar drawn again, the mode 0
 * (DS:ae57). Also the start of eclplay's --load (cok_drawing.load). False
 * where the run stopped. */
static bool load_saved(cok_adventure *game, const char *path)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    q->loaded_game = true; /* DS:b1ed */
    uint8_t frame_was = (uint8_t)cok_ecl_load_var(vm, 0x3a);
    if (!cok_adventure_restore(game, path) || !loaded(game, frame_was)) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    /* The game's speed is the mirror of variable 0x12 (set_speed), which the shared
     * waits read (attack.c's wait_speed, arena.c, cloud.c); the restore does not set it. */
    game->speed = (uint8_t)cok_ecl_load_var(vm, COK_VAR_SPEED);
    bottom_bar(game);
    vm->mode = 0;
    return true;
}

/* Load Saved Game (604b:2e87): "load from where? " Tdqk, Dkk or Exit
 * (Escape too); the saved games there listed by their letters, "load
 * which game: " with those alone (none: back); then "Loading...Please
 * Wait" in the bar (1a62:0046), DS:b1ed set, and the game read
 * (604b:2ab4, 28f8: cok_adventure_restore; Dkk's from --import, Death
 * Knights' save path DQK.CFG names: import_game), the bar drawn again and
 * the mode 0 (DS:ae57). False where the run stopped. */
static bool load_game(cok_adventure *game)
{
    queen *q = own(game);
    full_window(game); /* 20b2:0030 */
    static const char *const where[] = {"Tdqk", "Dkk", "Exit"}; /* DS:500c */
    cok_adventure_log(game, "menu", "Tdqk Dkk Exit");
    int choice;
    do {
        choice = bar_menu(game, "load from where? ", where, 3, false, false, 0, 0);
        if (choice < 0) return false;
    } while (choice > 2 && choice != 0x1b);
    log_choice(game, choice);
    if (choice == 2 || choice == 0x1b) return true;
    const cok_rules_import *I = cok_game_rules(game)->characters.import;
    bool other = choice == 1; /* DS:b7ac 4 */
    const cok_rules_saves *S = other ? &I->from->saves : &cok_game_rules(game)->saves;
    char dir[600];
    if (other)
        snprintf(dir, sizeof dir, "%s", game->import_dir); /* DQK.CFG's, DS:ace2 */
    else
        save_path(game, dir, sizeof dir);
    char letters[10];
    size_t count = saved_letters(dir, S->game, S->game_extension, letters);
    if (count == 0) return true;
    int letter = letter_menu(game, "load which game: ", letters, count);
    if (letter < 0) return false;
    if (letter == 0x1b) return true;
    log_choice(game, letter);
    bottom_bar(game);
    text(game, 8 * 0x18, 8, "Loading...Please Wait", 0x87); /* DS:601f */
    q->loaded_game = true; /* DS:b1ed */
    if (other) return import_game(game, dir, (char)('A' + letter));
    char name[64], path[700];
    snprintf(name, sizeof name, "%s%c.%s", S->game, 'A' + letter, S->game_extension);
    if (!find_file(dir, name, path, sizeof path)) {
        snprintf(game->error, sizeof game->error, "%.300s/%.60s: not found", dir, name);
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    return load_saved(game, path);
}

/* Save Current Game (604b:3395): "save which game: " A-J (the mode 2's,
 * camping's, small picture animating), Escape back; the save disk asked
 * for (604b:0073: the port's save path), "Saving...Please Wait" in the
 * bar, SAVGAM<letter>.QSV written (31a7: cok_saved_game_write, the item
 * table its fill); where its letter is not the vault's (DS:c3ea), the
 * vault's file copied to the new letter's (35bc reads the old, 'Z' none:
 * empty; 3600 writes it), the letter the vault's; the game saved
 * (DS:ae5b); the bar again. False where the run stopped. */
static bool save_game(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    const cok_rules *rules = cok_game_rules(game);
    static const char letters[] = "ABCDEFGHIJ";
    int letter;
    do {
        letter = letter_menu(game, "save which game: ", letters, 10);
        if (letter < 0) return false;
    } while (letter == 0x1b && false);
    if (letter == 0x1b) return true;
    log_choice(game, letter);
    if (!writable(game, "Save: no save path (--saves)")) return true;
    bottom_bar(game);
    text(game, 8 * 0x18, 8, "Saving...Please Wait", 0x87); /* DS:604d */
    static cok_saved_game saved;
    memset(&saved, 0, sizeof saved);
    for (size_t i = 0; i < 0x400; ++i) saved.mem4b00[i] = vm->mem4b00[i];
    saved.map_x = vm->map_x;
    saved.map_y = vm->map_y;
    saved.direction = vm->direction;
    saved.ahead = vm->ahead;
    saved.square = vm->square;
    saved.last_mode = vm->last_mode;
    saved.mode = vm->mode;
    saved.wall_ids[0] = game->wall_ids[0];
    saved.extra = game->saved_extra;
    saved.count = (uint8_t)game->party.count;
    for (size_t i = 0; i < game->party.count; ++i) saved.characters[i] = game->party.members[i];
    saved.fill = &game->item_table[0][0];
    saved.fill_size = sizeof game->item_table;
    char path[700], from[700];
    save_path(game, path, sizeof path);
    size_t n = strlen(path);
    snprintf(path + n, sizeof path - n, "/%s%c.%s", rules->saves.game, letters[letter],
             rules->saves.game_extension);
    bool ok = cok_saved_game_write(path, rules, &saved, game->error, sizeof game->error);
    if (ok && game->vault_letter != letters[letter]) {
        cok_vault vault = {0};
        bool missing = false;
        char name[32];
        if (game->vault_letter != 'Z') {
            save_path(game, from, sizeof from);
            snprintf(name, sizeof name, "%s%c.%s", rules->saves.vault, game->vault_letter,
                     rules->saves.vault_extension);
            char found[700];
            if (find_file(from, name, found, sizeof found) &&
                !cok_vault_read_game(found, rules, &vault, &missing, game->error,
                                     sizeof game->error))
                ok = false;
        }
        game->vault_letter = letters[letter];
        save_path(game, from, sizeof from);
        n = strlen(from);
        snprintf(from + n, sizeof from - n, "/%s%c.%s", rules->saves.vault, letters[letter],
                 rules->saves.vault_extension);
        if (ok && !cok_vault_write_game(from, rules, &vault, &game->item_table[0][0],
                                        sizeof game->item_table, game->error,
                                        sizeof game->error))
            ok = false;
        cok_vault_free(&vault);
    }
    if (!ok) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    cok_adventure_log(game, "save", path);
    q->saved = true;
    bottom_bar(game);
    return true;
}

/* The selected member removed (177e:1e31): the selection the one before it, or after it where
 * it was the first, or the first; the party's size (variable 0x20) one less when counted (1f12:
 * 1e31's first argument 0; 11b0 passes 1 for a record past the party). */
static void remove_selected(cok_adventure *game, bool counted)
{
    cok_ecl *vm = &game->vm;
    size_t i = cok_party_index(&game->party, vm->character);
    if (i >= game->party.count) return;
    queen_remove_icons(game, vm->character); /* 177e:1e9d's flag 1 (204c:004d) */
    cok_party_remove(&game->party, i);
    vm->character = cok_party_record(&game->party, i > 0 ? i - 1 : 0);
    if (counted) cok_ecl_add_var(vm, COK_VAR_PARTY_SIZE, -1);
}

/* 604b:23a7 with no name: the character written to the save path as
 * <file name>.QCH (5e98:0044); where that file is there, "Update
 * <name>? " (DS:5fe3) Yes writes it, No asks "New file name: " (1a62:05c9,
 * DS:5fee) until a name gives one (Escape there: nothing written) and
 * asks again of it. False where the run stopped. */
static bool write_character(cok_adventure *game, const cok_character *character)
{
    const cok_rules *rules = cok_game_rules(game);
    char name[17], base[9], dir[600], file[64], found[700];
    cok_field_name(rules->character, character->record, name);
    file_name(name, base);
    save_path(game, dir, sizeof dir);
    for (;;) {
        snprintf(file, sizeof file, "%s.%s", base, rules->saves.roster);
        if (!find_file(dir, file, found, sizeof found)) break;
        full_window(game); /* 20b2:0030 */
        char line[64];
        snprintf(line, sizeof line, "Update %s?", name);
        int yes = yes_no(game, line, false);
        if (yes < 0) return false;
        if (yes) break;
        bottom_bar(game);
        for (;;) {
            prompt_box(game, 0, "New file name:", 0, 7);
            char typed[40];
            int done = edit_line(game, typed, 35);
            if (done < 0) return false;
            if (done == 0) return true;
            file_name(typed, base);
            if (base[0] != '\0') break;
        }
    }
    if (!writable(game, "Remove: no save path (--saves)")) return true;
    if (!cok_character_write_file(character, rules, dir, base, rules->saves.roster,
                                  game->error, sizeof game->error)) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    cok_adventure_log(game, "save", file);
    return true;
}

/* Remove Character (7368:066f): a member (+0x9b below 0x80) written
 * (604b:23a7) then removed (177e:1e31); an NPC (+0x9b bit 7) stops, though
 * 7368:0ee9 is alt_drop's Drop ("Drop %s forever? "): 066f's screen before 0ee9
 * is the adventure's, not Alt's camp, and no check runs it. False where the
 * run stopped. */
static bool remove_character(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    size_t i = cok_party_index(&game->party, vm->character);
    if (i >= game->party.count) return true;
    if (cok_field_u8(cok_game_rules(game)->character, vm->character, COK_FIELD_NPC) >= 0x80) {
        cok_adventure_not_yet(game, "Remove Character of an NPC", "dqk 7368:0ee9");
        return false;
    }
    if (!write_character(game, game->party.members[i])) return false;
    remove_selected(game, true);
    return true;
}

/* The camp's buttons (7368:01a7) in the order made, which is the index
 * returned: their words (DS:7dce-7e84), hotkeys, row and column. */
static const struct {
    const char *text;
    char key;
    int row, col;
} camp_items[] = {
    {"Create Character", 'C', 118, 168},  {"Delete Character", 'D', 132, 168},
    {"Modify Character", 'M', 146, 8},    {"Train Character", 'T', 160, 8},
    {"Knight new Order", 'K', 174, 8},    {"View Character", 'V', 188, 8},
    {"Add Character", 'A', 118, 8},       {"Remove Character", 'R', 132, 8},
    {"Load Saved Game", 'L', 146, 168},   {"Save Current Game", 'S', 160, 168},
    {"Begin Adventuring", 'B', 174, 168}, {"Exit From Game", 'E', 188, 168},
};
enum {
    CAMP_ITEMS = sizeof camp_items / sizeof *camp_items, CAMP_WIDTH = 18,
    CAMP_CREATE = 0, CAMP_DELETE, CAMP_MODIFY, CAMP_TRAIN, CAMP_KNIGHT, CAMP_VIEW, CAMP_ADD,
    CAMP_REMOVE, CAMP_LOAD, CAMP_SAVE, CAMP_BEGIN, CAMP_EXIT,
    CAMP_SELECTED = 0x10, /* Up or Down changed the selected member */
};

/* Which of the camp's buttons are on (DS:3c3e-3c49, 7368:046f): with a
 * member selected all but Train and Knight new Order where variable 0x30
 * is 0 and DS:bd92 too, and Add where six non-NPCs are in; without, Create,
 * Delete, Add, Load and Exit. */
static void camp_flags(cok_adventure *game, bool on[CAMP_ITEMS])
{
    cok_ecl *vm = &game->vm;
    for (size_t i = 0; i < CAMP_ITEMS; ++i) on[i] = true;
    if (vm->character != NULL) {
        on[CAMP_TRAIN] = on[CAMP_KNIGHT] =
            cok_ecl_load_var(vm, 0x30) > 0 || own(game)->train_anywhere;
        size_t members = 0;
        for (size_t i = 0; i < game->party.count; ++i)
            if (cok_field_u8(cok_game_rules(game)->character, game->party.members[i]->record,
                             COK_FIELD_NPC) < 0x80)
                ++members;
        if (members > 5) on[CAMP_ADD] = false;
    } else {
        on[CAMP_MODIFY] = on[CAMP_TRAIN] = on[CAMP_KNIGHT] = on[CAMP_VIEW] = false;
        on[CAMP_REMOVE] = on[CAMP_SAVE] = on[CAMP_BEGIN] = false;
    }
}

/* 7368:01a7: the buttons, those off sunken with no hotkey (2aa6:188f's
 * 0x10), read by their letters or a click; Up and Down (the list's keys)
 * select the member before or after, wrapping, and give CAMP_SELECTED;
 * Alt-S and Alt-T wait on; the rest beep. -1 if the keys ran out. */
static int camp_menu(cok_adventure *game, const bool on[CAMP_ITEMS])
{
    cok_ecl *vm = &game->vm;
    controls_clear(game);
    rect rects[CAMP_ITEMS];
    char line[512] = "";
    for (size_t i = 0; i < CAMP_ITEMS; ++i) {
        button(game, camp_items[i].row, camp_items[i].col, camp_items[i].text, CAMP_WIDTH,
               on[i] ? 0 : BUTTON_DISABLED);
        rects[i] = button_rect(camp_items[i].row, camp_items[i].col, CAMP_WIDTH);
        control_add(game, rects[i]);
        if (on[i]) {
            size_t n = strlen(line);
            snprintf(line + n, sizeof line - n, "%s%s", n > 0 ? " " : "", camp_items[i].text);
        }
    }
    cok_adventure_log(game, "menu", line);
    for (;;) {
        input in;
        int key = read_input(game, &in);
        if (key < 0 && key != COK_KEY_TIMEOUT) {
            controls_clear(game);
            return -1;
        }
        for (size_t i = 0; i < CAMP_ITEMS; ++i) {
            if (!on[i]) continue;
            if (in.click ? inside(&rects[i], in.x, in.y) : hotkey(camp_items[i].key, in.key)) {
                controls_clear(game);
                button(game, camp_items[i].row, camp_items[i].col, camp_items[i].text,
                       CAMP_WIDTH, BUTTON_PRESSED);
                log_choice(game, (int)i);
                return (int)i;
            }
        }
        if (!in.click && (in.key == KEY_UP || in.key == KEY_DOWN) && game->party.count > 0) {
            size_t i = cok_party_index(&game->party, vm->character), n = game->party.count;
            i = in.key == KEY_DOWN ? (i + 1 < n ? i + 1 : 0) : (i > 0 && i < n ? i - 1 : n - 1);
            vm->character = cok_party_record(&game->party, i);
            controls_clear(game);
            return CAMP_SELECTED;
        }
        if (in.key != KEY_ALT + 'S' && in.key != KEY_ALT + 'T') beep(game);
    }
}

/* Begin Adventuring's part of the camp (7368:07af-0804): the mode the last
 * was (the last mode, or 4 for none); a game not loaded (DS:b1ed) and small
 * picture 9 not shown (DS:ae42): the overland's frame in mode 3 on the
 * overland, else the adventure's screen, the party's list after either; a
 * game loaded: by the mode, 0 and 4 the adventure's screen and the list, 3
 * the overland's frame, the others nothing. Then the bar, variable 0x30 0,
 * the game not saved. */
static void begin_adventuring(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    vm->mode = vm->last_mode != 0 ? vm->last_mode : 4;
    if (!q->loaded_game && q->picture_id != 9) {
        if (vm->mode == 3 && cok_ecl_load_var(vm, 0x24) == 1)
            overland_screen(game);
        else
            adventure_screen(game);
        party_list(game);
    } else if (vm->mode == 0 || vm->mode == 4) {
        adventure_screen(game);
        party_list(game);
    } else if (vm->mode == 3) {
        overland_screen(game);
    }
    bottom_bar(game);
    cok_ecl_store(vm, 0x30, 0);
    q->saved = false;
}

/* The camp's menu (7368:046f; from_main is its argument: 1 from main, 0
 * from the adventure's PROGRAM 0 (5f07:0c15); the adventure's Encamp has its
 * own menu, encamp_menu): the mode 0, the last mode the one it
 * was (DS:ae57, ae67); its screen (20b2:03ce; not again after a key that
 * is no button's), the party's list where a member is selected
 * (5e22:023b), its buttons (camp_flags). Then the full window (20b2:0030)
 * but for Remove, and: Create (66e5:2f95: queen_create.c), Modify
 * (6a87:1293: queen_modify.c), Train (6be8:0022) and Knight new Order
 * (7368:10c2: queen_train.c);
 * Remove, the party emptied: from main a new game and the mode 0 (2d99:1161),
 * from the adventure DS:b7a7 set instead (7368:06dc); Load (a party not saved
 * first asks "Game not saved. Load anyway? ", DS:7e93), the party emptied,
 * the game loaded, or a new game's where none loads (64b2:04ec); from the
 * adventure the load is asked (7368:0758: DS:acc4 by 5f07:0004, DS:b7a7 by 7368:0761;
 * the run ends and the adventure's loop loads it); Save; Begin Adventuring (with a party) returns,
 * the mode the last (4 for none), the adventure's screen drawn as the mode
 * has it, variable 0x30 0, the game not saved; Exit
 * From Game asks "Exit to DOS? " and, a party not saved, "Game not saved.
 * Exit anyway? " (No saving it: 604b:3395), then quits (2093:002d).
 * Returns the run's status, COK_ECL_OK with *begin set for Begin. */
static cok_ecl_status camp(cok_adventure *game, bool from_main, bool *begin)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    *begin = false;
    vm->last_mode = vm->mode;
    vm->mode = 0;
    enter_state(game, STATE_CAMP);
    int si = 1;
    for (;;) {
        if (si <= CAMP_EXIT && !camp_screen(game)) {
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return vm->status;
        }
        if (vm->character != NULL) party_list(game);
        bool on[CAMP_ITEMS];
        camp_flags(game, on);
        si = camp_menu(game, on);
        if (si < 0) return vm->status;
        if (si != CAMP_REMOVE && si <= CAMP_EXIT) full_window(game);
        if (si == CAMP_DELETE) party_list(game);
        switch (si) {
        case CAMP_CREATE:
            if (!queen_create(game)) return vm->status;
            break;
        case CAMP_DELETE:
            if (!delete_character(game)) return vm->status;
            break;
        case CAMP_MODIFY: {
            /* 7368:060c: 6a87:1293 for the selected member; the game
             * not saved after. */
            size_t member = cok_party_index(&game->party, vm->character);
            if (member < game->party.count &&
                !queen_modify(game, game->party.members[member], false))
                return vm->status;
            q->saved = false;
            break;
        }
        case CAMP_TRAIN: {
            size_t i = cok_party_index(&game->party, vm->character);
            bool nothing;
            if (i < game->party.count && !queen_train(game, game->party.members[i], false, &nothing))
                return vm->status;
            q->saved = false; /* 7368:061b */
            break;
        }
        case CAMP_KNIGHT: {
            size_t i = cok_party_index(&game->party, vm->character);
            if (i < game->party.count && !queen_knight_order(game, game->party.members[i]))
                return vm->status;
            break;
        }
        case CAMP_VIEW:
            if (!view_character(game, NULL)) return vm->status;
            break;
        case CAMP_ADD:
            if (!add_character(game)) return vm->status;
            q->saved = false;
            break;
        case CAMP_REMOVE:
            if (!remove_character(game)) return vm->status;
            if (game->party.count == 0) {
                if (from_main) {
                    new_game(game); /* 2d99:1161: 64b2:04ec */
                    vm->mode = 0;
                } else {
                    q->b7a7 = true; /* 7368:06dc */
                }
            }
            q->saved = false; /* 7368:061b */
            break;
        case CAMP_LOAD:
            if (game->party.count > 0 && !q->saved) {
                int yes = yes_no(game, "Game not saved. Load anyway? ", true);
                if (yes < 0) return vm->status;
                if (!yes) break;
            }
            if (!from_main) {
                /* 7368:0758: 5f07:0004 sets DS:acc4 (1), 7368:0761 DS:b7a7: the
                 * camp returns to the ECL without loading (the loop loads it). */
                q->load_asked = q->b7a7 = true;
                vm->halt = true; /* the run ends (5f07:0fc4) */
                return vm->status;
            }
            cok_party_free(&game->party); /* 177e:1e31 each */
            vm->character = NULL;
            cok_ecl_store(vm, 0x20, 0);
            if (!load_game(game)) return vm->status;
            if (game->party.count == 0) {
                new_game(game);
                vm->last_mode = vm->mode;
                vm->mode = 0;
            }
            break;
        case CAMP_SAVE:
            if (game->party.count > 0 && !save_game(game)) return vm->status;
            break;
        case CAMP_BEGIN:
            begin_adventuring(game);
            *begin = true;
            return COK_ECL_OK;
        case CAMP_EXIT: {
            int yes = yes_no(game, "Exit to DOS? ", false);
            if (yes < 0) return vm->status;
            if (!yes) break;
            if (game->party.count > 0 && !q->saved) {
                yes = yes_no(game, "Game not saved. Exit anyway? ", false);
                if (yes < 0) return vm->status;
                if (!yes) {
                    if (!save_game(game)) return vm->status;
                    break;
                }
            }
            cok_adventure_quit(game);
            return COK_ECL_OK;
        }
        default: break;
        }
    }
}

/* The game's clock (1396:044d): count of unit (the variables 5-11: a
 * tenth of a minute?, the minutes, their tens, the hours, days, months,
 * years) passed, each one then carried (1396:0368: DS:634b's 10, 10, 6,
 * 24, 30, 12 and 100 of each; a hundred years ageing the party a year,
 * 0x52, the years less 100; a day turning the moons, 1396:00f8); then the
 * party's effects' minutes (1396:019f) by that many minutes, ten at a
 * time: an effect's minutes, where it has some, less them, or, where
 * that runs them out, the effect ended (148f:017d, not ported yet: the
 * run stops). False where it stopped.
 *
 * 019f's camp case (mode 2, Encamp's: encamp, PROGRAM 9; not modelled here) reads
 * the members' flags DS:a260 for [2..party size + 1], one past the party, and
 * sets a flag where an effect is reduced or ends; the other modes have every
 * flag at 1 (memset, 0x35 bytes). Here every member is counted in every mode,
 * as Champions' clock does; that is the difference Encamp will need. */
static bool moon(cok_adventure *game, uint16_t at, uint8_t days, unsigned base);

/* Whether node is still in c's chain (NULL, the head, always is). */
static bool linked(const cok_character *c, const cok_effect *node)
{
    if (node == NULL) return true;
    for (const cok_effect *e = c->effects; e != NULL; e = e->next)
        if (e == node) return true;
    return false;
}

static bool pass_time(cok_adventure *game, unsigned count, unsigned unit)
{
    static const uint16_t limits[7] = {10, 10, 6, 24, 30, 12, 100}; /* DS:634b */
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    unsigned units[7];
    for (unsigned i = 0; i < 7; ++i) units[i] = cok_ecl_load_var(vm, (uint16_t)(5 + i));
    for (unsigned n = 0; n < count; ++n) {
        ++units[unit];
        for (unsigned k = 0; k < 7; ++k) {
            if (units[k] < limits[k]) continue;
            if (k == 6) {
                for (size_t i = 0; i < game->party.count; ++i) {
                    uint8_t *c = game->party.members[i]->record;
                    cok_field_set16(L, c, COK_FIELD_AGE,
                                    (uint16_t)(cok_field_u16(L, c, COK_FIELD_AGE) + 1));
                }
                units[6] -= limits[6];
                game->effects.aging = true; /* 1396:0368: DS:bce7 = 1 */
                continue;
            }
            ++units[k + 1];
            units[k] -= limits[k];
            if (k == 3) {
                /* 1396:00f8: Solinari, Lunitari and Nuitari, a phase
                 * every 8, 1 and 6 days. */
                moon(game, 0x1fc, 8, 0x1d);
                moon(game, 0x1fd, 1, 0x21);
                moon(game, 0x1fe, 6, 0x25);
            }
        }
    }
    for (unsigned i = 0; i < 7; ++i) cok_ecl_store(vm, (uint16_t)(5 + i), (uint16_t)(uint8_t)units[i]);
    uint16_t minutes = (uint16_t)count; /* 019f multiplies words */
    for (unsigned k = 1; k < unit; ++k) minutes = (uint16_t)(minutes * limits[k]);
    queen *q = own(game);
    if (vm->mode == 2) {
        /* 019f's scan: a260[2] to a260[party + 1] (the scan stops at the first flag set). */
        unsigned party = cok_ecl_var(vm, COK_VAR_PARTY_SIZE);
        unsigned last = party + 1 > 2 ? party + 1 : 2;
        if (last >= sizeof q->timed)
            return cok_adventure_not_yet(game, "the camp's timers past DS:a260", "dqk 1396:019f");
        bool any = false;
        for (unsigned i = 2; i <= last && !any; ++i) any = q->timed[i] != 0;
        if (!any) return true;
    } else {
        memset(q->timed, 1, sizeof q->timed);
    }
    while (minutes > 0) {
        uint16_t step = (uint16_t)(minutes < 11 ? minutes : 10);
        for (size_t r = 0; r < game->party.count; ++r) {
            unsigned i = (unsigned)r + 1;
            if (i >= sizeof q->timed)
                return cok_adventure_not_yet(game, "the timers past DS:a260", "dqk 1396:019f");
            if (q->timed[i] == 0) continue;
            q->timed[i] = 0;
            cok_character *c = game->party.members[r];
            /* The last node the walk kept (NULL: the chain's head), its safe place to go on
             * from. */
            cok_effect *kept = NULL;
            cok_effect *e = c->effects;
            while (e != NULL) {
                if (e->duration == 0) {
                    kept = e;
                    e = e->next;
                    continue;
                }
                if (step < e->duration) {
                    e->duration = (uint16_t)(e->duration - step);
                    q->timed[i] = 1;
                    kept = e;
                    e = e->next;
                    continue;
                }
                uint8_t id = e->id;
                if (!cok_effects_remove(&game->effects, c, e, id))
                    return cok_adventure_effect_failed(game);
                if (id == 0xc4 && vm->mode != 5 && vm->mode != 3 && vm->mode != 0 && vm->mode != 10)
                    return cok_adventure_not_yet(game, "the effect 0xc4's call (5e22:0065)",
                                                 "dqk 5e22:0065");
                q->timed[i] = 1;
                /* The removal may have run a handler that removed other nodes, the next one
                 * among them, and freed it (e is freed too), so the walk does not follow the
                 * next read before the removal. It goes on from the kept node's link as it
                 * is now; a kept node the handler removed has no place to go on from, cited. */
                if (!linked(c, kept))
                    return cok_adventure_not_yet(game, "an effect's removal that removes the "
                                                 "timers' kept node (1396:019f's walk)",
                                                 "dqk 1396:019f");
                e = kept != NULL ? kept->next : c->effects;
            }
        }
        minutes = (uint16_t)(minutes - step);
    }
    return true;
}

/* A moon (1396:000e): its days (variable at + 1) up to its own, then its
 * phase (at - 2: 0-3) the next, its lamp (FRAME base + phase) drawn but on
 * the overland, the days 0; else a day more. */
static bool moon(cok_adventure *game, uint16_t at, uint8_t days, unsigned base)
{
    cok_ecl *vm = &game->vm;
    uint16_t day = cok_ecl_load_var(vm, (uint16_t)(at + 1));
    if (day < days) {
        cok_ecl_store(vm, (uint16_t)(at + 1), (uint16_t)(day + 1));
        return false;
    }
    uint16_t phase = cok_ecl_load_var(vm, (uint16_t)(at - 2));
    cok_ecl_store(vm, (uint16_t)(at - 2), (uint16_t)(phase >= 3 ? 0 : phase + 1));
    if (vm->mode != 3)
        draw_frame_record(game, base + cok_ecl_load_var(vm, (uint16_t)(at - 2)), 0, 0, NULL);
    cok_ecl_store(vm, (uint16_t)(at + 1), 0);
    return true;
}

/* The 3D view drawn again in its box (8a8b:085a) after a turn, the wall
 * ahead (8a8b:0628) first. */
static void turn(cok_adventure *game, unsigned by)
{
    cok_ecl *vm = &game->vm;
    vm->direction = (uint8_t)((vm->direction + by) % 8);
    vm->ahead = cok_view_wall(&game->view, vm->direction, vm->map_x, vm->map_y);
    char line[24];
    snprintf(line, sizeof line, "%d,%d facing %u", vm->map_x, vm->map_y, vm->direction);
    cok_adventure_log(game, "turn", line);
    draw_view(game);
}

/* Up (6fea:000a): the edge variable 0x21 cleared; where the way ahead is
 * open (8a8b:04a4), a step off the map's width or height puts the party
 * at its edge and sets 0x21. */
static void check_step(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    const cok_view *v = &game->view;
    cok_ecl_store(vm, 0x21, 0);
    if (cok_view_passage(v, vm->direction, vm->map_x, vm->map_y) == 0) return;
    int x = vm->map_x + step_x[vm->direction & 7], y = vm->map_y + step_y[vm->direction & 7];
    int w = v->geo_header[1], h = v->geo_header[0];
    if (x > w - 1) {
        vm->map_x = (int8_t)(w - 1);
        cok_ecl_store(vm, 0x21, 1);
    }
    if (x < 0) {
        vm->map_x = 0;
        cok_ecl_store(vm, 0x21, 1);
    }
    if (y > h - 1) {
        vm->map_y = (int8_t)(h - 1);
        cok_ecl_store(vm, 0x21, 1);
    }
    if (y < 0) {
        vm->map_y = 0;
        cok_ecl_store(vm, 0x21, 1);
    }
}

static bool travel(cok_adventure *game);
static bool step_ahead(cok_adventure *game);

/* The step (6fea:0753): on the overland (mode 3) with variable 3 below
 * 0xff its travel, with 0xff nothing; elsewhere but in 3D (mode 4) with
 * variable 3 below 0xff variable 3 0; then the small picture freed
 * (63ae:02b0). In 3D (step_ahead), with variable 3 below 0xff: where
 * the way ahead is open, 6fea:00c6: sound 11 (not sounded yet), 50
 * hundredths... milliseconds (1df9:052a), the square ahead, wrapping at
 * the map's edges, its wall ahead and byte, a minute passed (ten
 * searching, variable 0x19's bit 0); then the status. False where the
 * time stopped the run. */
static bool step(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    bool ok = true;
    if (vm->mode == 3) {
        if (cok_ecl_load_var(vm, 3) < 0xff) ok = travel(game);
    } else if (vm->mode != 4 || cok_ecl_load_var(vm, 3) >= 0xff) {
        cok_ecl_store(vm, 3, 0);
    } else {
        ok = step_ahead(game);
    }
    /* 63ae:02b0: the small picture freed. */
    q->picture_loaded = false;
    q->picture_id = 0xff;
    return ok;
}

/* 6fea:01a7, the overland's travel: the mark taken off (7065:0072), the
 * square kept in variables 0x17 and 0x18, a step the way it faces within
 * 0-37 across and 0-14 down into 0x25 and 0x26, the mark again
 * (7065:0011), twelve hours passed (1396:044d with 12 of unit 3). */
static bool travel(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    unmark(game);
    int x = cok_ecl_load_var(vm, 0x25), y = cok_ecl_load_var(vm, 0x26);
    cok_ecl_store(vm, 0x17, (uint16_t)x);
    cok_ecl_store(vm, 0x18, (uint16_t)y);
    x = (int8_t)(x + step_x[vm->direction & 7]);
    y = (int8_t)(y + step_y[vm->direction & 7]);
    x = x < 0 ? 0 : x > 0x25 ? 0x25 : x;
    y = y < 0 ? 0 : y > 0x0e ? 0x0e : y;
    cok_ecl_store(vm, 0x25, (uint16_t)x);
    cok_ecl_store(vm, 0x26, (uint16_t)y);
    marker(game);
    return pass_time(game, 12, 3);
}

static bool step_ahead(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    const cok_view *v = &game->view;
    if (cok_view_passage(v, vm->direction, vm->map_x, vm->map_y) == 1) {
        pass(game, 50);
        int w = v->geo_header[1], h = v->geo_header[0];
        int x = vm->map_x + step_x[vm->direction & 7], y = vm->map_y + step_y[vm->direction & 7];
        if (x < 0) x = w - 1;
        if (x > w - 1) x = 0;
        if (y < 0) y = h - 1;
        if (y > h - 1) y = 0;
        vm->map_x = (int8_t)x;
        vm->map_y = (int8_t)y;
        vm->ahead = cok_view_wall(v, vm->direction, vm->map_x, vm->map_y);
        vm->square = cok_view_square(v, vm->map_x, vm->map_y);
        char line[24];
        snprintf(line, sizeof line, "%d,%d facing %u", vm->map_x, vm->map_y, vm->direction);
        cok_adventure_log(game, "step", line);
        if (!pass_time(game, 1, (cok_ecl_load_var(vm, 0x19) & 1) ? 2 : 1)) return false;
    }
    status_line(game);
    return true;
}

/* The view's areas of the bar menu (1bdb:1aef's four, 1b45-1bc5, in 2 px
 * units from 8000: its rects {top, left, bottom, right}), in the order of
 * 1206's codes (126e: r - DS:a658 - 2): turn left (0x86, ALWAYS.TLB 26),
 * step ahead (0x88, 11), turn right (0x82, 25), turn around (0x84, 27). */
static const rect view_areas[] = {{0, 0, 100, 40}, {0, 40, 100, 96}, {0, 96, 100, 136},
                                  {100, 0, 160, 136}};
static const uint8_t view_cursors[] = {26, 11, 25, 27};
static const int view_codes[] = {0x86, 0x88, 0x82, 0x84};

/* The adventure's commands in 3D (6fea:0365, mode 4): variable 3 0, then
 * the bar's "Area Cast View Encamp Search Look Inv" (DS:5088 but Move,
 * 1bdb:0005; 1aef: the extended keys' filter) until one ends it: Right,
 * Down and Left turn the party (a quarter, a half, three quarters) and
 * draw the view and status again; Up checks the step (6fea:000a), the
 * status drawn, and ends it (giving 0xff); Escape nothing; another
 * filtered key picks a member (5e22:067c, not ported yet). With the mouse
 * on, a click on the view's areas gives the same codes as those keys
 * (view_areas; 1aef adds them to a fresh list only: 1b3b tests DS:a6b5,
 * the rebuilt flag of 1bdb:0d26, and the port's gate is mouse.on). Encamp
 * ends it (4); Search turns variable 0x19's bit 0 and draws the status;
 * Look sets its bit 1, draws the status, passes ten minutes and ends it
 * (6); Area, Cast, View and Inv are not ported yet (the run stops). Then
 * the text window cleared where text was shown
 * (DS:b2fb). Returns the command, -1 where the run stopped. */
static int commands(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_store(vm, 3, 0);
    mouse_start(game);
    static const char *const items[] = {"Area", "Cast", "View", "Encamp", "Search", "Look",
                                        "Inv"};
    /* 1aef adds the view's areas to a fresh list only (1b3b); the port adds them with the mouse on. */
    size_t area_count = game->mouse.on ? sizeof view_areas / sizeof view_areas[0] : 0;
    int result = -1;
    for (bool done = false; !done;) {
        cok_adventure_log(game, "menu", "Area Cast View Encamp Search Look Inv");
        int choice = bar_menu_areas(game, "", items, 7, true, false, 0, 0, view_areas,
                                    view_cursors, area_count, -1, false);
        if (choice < 0) return -1;
        if (choice >= AREA_MENU) choice = view_codes[choice - AREA_MENU];
        if (choice >= 0x80 || choice == 0x1b) {
            switch (choice) {
            case 0x1b: break;
            case 0x82: turn(game, 2); status_line(game); break;
            case 0x84: turn(game, 4); status_line(game); break;
            case 0x86: turn(game, 6); status_line(game); break;
            case 0x88:
                check_step(game);
                status_line(game);
                result = 0xff;
                done = true;
                break;
            default:
                cok_adventure_not_yet(game, "a member picked by a key", "dqk 5e22:067c");
                return -1;
            }
            continue;
        }
        log_choice(game, choice + 1);
        switch (choice + 1) {
        case 1: cok_adventure_not_yet(game, "Area", "dqk 6fea:0427"); return -1;
        case 2: cok_adventure_not_yet(game, "Cast", "dqk 6070:0688"); return -1;
        case 3:
            /* 2d99:0fef, then the screen as it was (177e:1ad1). */
            if (!view_character(game, NULL)) return -1;
            adventure_screen(game);
            party_list(game);
            status_line(game);
            draw_view(game);
            break;
        case 4: result = 4; done = true; break;
        case 5:
            cok_ecl_store(vm, 0x19, cok_ecl_load_var(vm, 0x19) ^ 1);
            status_line(game);
            break;
        case 6:
            cok_ecl_store(vm, 0x19, cok_ecl_load_var(vm, 0x19) | 2);
            status_line(game);
            if (!pass_time(game, 1, 2)) return -1;
            result = 6;
            done = true;
            break;
        default: cok_adventure_not_yet(game, "Inv", "dqk 6fea:0241"); return -1;
        }
    }
    if (game->text_shown) {
        clear_cells(game, 1, 0x11, 0x26, 0x16);
        game->text_shown = false;
    }
    return result;
}

/* The adventure's run is left while DS:b7a7 is set (5f07:126c, 12ab, 12bc, 12da, 131d,
 * 132e): a load asked from the camp (DS:acc4 with it) or the party emptied. */
static bool ended(cok_adventure *game)
{
    return own(game)->b7a7;
}

/* Begin Adventuring's start (5ebd:00ef after 7368:046f): the copy
 * protection (65cc:008b: a word from the journal, the game ending where
 * it is wrong; DARK-QUEEN.md's decision: logged and passed over, its
 * checks of DS:bec0-bec2 too, its two random draws made), the adventure's
 * screen (20b2:00e4), the party's list and the status (or, on the overland,
 * its frame), then the game (5f07:10cd): the mode 4 (3 outside 3D, variable
 * 0x22 0), the block variable 0x16's or a new game's (4, its list drawn
 * first), entered as the ECL's NEWECL enters one (cok_adventure_enter); then
 * its loop: a command (6fea:0365); while it is Look (variable 0x19 above 1) or
 * Encamp, Look's: the view again and the location vector, searching or
 * not as before; Encamp (encamp) then the next command; then the
 * after-move vector; the last square in variables 0x17 and 0x18, the
 * step (6fea:0753), the view and the location vector, each NEWECL
 * entering its block (5f07:0ff4: cok_adventure_reenter). Outside 3D
 * (mode 3) its loop is not ported yet. The block is the variable 0x16's
 * (the start's), or the play's (eclplay's --play). */

static cok_ecl_status adventure(cok_adventure *game, uint8_t block, bool begun)
{
    cok_ecl *vm = &game->vm;
    /* The copy protection is asked at Begin Adventuring (5ebd:00ef), not when main takes the
     * camp again after a load (5ebd:0331-033a). */
    if (begun) {
        /* The protection's two draws (65cc:00cb random(20) for the journal entry, 00d4-00de random(3)
         * for the word). Its redraw of the word (00fa-00ff, while DS:6dd2 + 0x14*entry + 5 + word is
         * 0xff) never fires with the shipped table, so it is not ported. The question and its check
         * draw nothing. A wrong answer draws nothing more: 65cc:0298 is a plain jump past the retry
         * (029a-02b0, which would ask again with new draws), so the question is logged and passed
         * over (DARK-QUEEN.md decision 4). */
        cok_tp_random(&game->vm.seed, 20);
        cok_tp_random(&game->vm.seed, 3);
        cok_adventure_log(game, "protection", "the journal's word, passed over (dqk 65cc:008b)");
    }
    if (vm->mode == 3 && cok_ecl_load_var(vm, 0x24) == 1) {
        overland_screen(game);
    } else {
        adventure_screen(game);
        party_list(game);
        status_line(game);
    }
    /* A loaded game's first block keeps its locals (6c70:0001 with DS:b1ed
     * set, cok_adventure_restore's keep_vars); 10cd clears b1ed once it
     * has entered it. */
    cok_ecl_status status = cok_adventure_enter(game, block != 0 ? block : 4);
    own(game)->loaded_game = false;
    vm->keep_vars = false;
    /* 5f07:1164-11ed: the entry's run is followed by no DS:b7a7 test. After a lost party the
     * menu (11f3, 1d99:1369) waits with the party empty and b7a7 kept; the run goes on only
     * after that command (not ported). */
    if (status == COK_ECL_OK && !vm->abort && own(game)->b7a7) {
        cok_adventure_not_yet(game, "the adventure's menu with DS:b7a7 set (a lost party, or a "
                                    "load asked: the party empty, b7a7 kept)",
                              "dqk 5f07:11f3 (1164-11ed: no b7a7 test after the entry's run)");
        return vm->status;
    }
    /* DS:b7a7 ends the run and the commands: nothing after it runs here (5f07:126c skips
     * the next command). */
    while (status == COK_ECL_OK && !vm->abort && !ended(game)) {
        if (vm->mode != 4) {
            cok_adventure_not_yet(game, "the adventure's commands outside 3D", "dqk 6fea:0605");
            return vm->status;
        }
        int key = commands(game);
        if (key < 0) return vm->status;
        while (status == COK_ECL_OK && !vm->abort && !ended(game) &&
               (cok_ecl_load_var(vm, 0x19) > 1 || key == 4)) {
            if (key == 4) {
                /* Encamp (5f07:122c calls 0b3b), then the next command (127a). */
                status = encamp(game);
            } else {
                uint16_t searching = cok_ecl_load_var(vm, 0x19) & 1;
                cok_ecl_store(vm, 0x19, 1);
                cok_adventure_view(game);
                status = cok_ecl_run(vm, vm->vectors[1]);
                if (status == COK_ECL_OK && !vm->abort && vm->reload)
                    status = cok_adventure_reenter(game);
                cok_ecl_store(vm, 0x19, searching);
            }
            if (status != COK_ECL_OK || vm->abort || ended(game)) break;
            key = commands(game);
            if (key < 0) return vm->status;
        }
        if (status != COK_ECL_OK || vm->abort || ended(game)) break;
        status = cok_ecl_run(vm, vm->vectors[0]);
        if (status != COK_ECL_OK || vm->abort || ended(game)) break;
        if (vm->reload) {
            status = cok_adventure_reenter(game);
            continue;
        }
        cok_ecl_store(vm, 0x17, (uint16_t)(uint8_t)vm->map_x);
        cok_ecl_store(vm, 0x18, (uint16_t)(uint8_t)vm->map_y);
        if (!step(game)) return vm->status;
        cok_adventure_view(game);
        status = cok_ecl_run(vm, vm->vectors[1]);
        if (status == COK_ECL_OK && !vm->abort && vm->reload) status = cok_adventure_reenter(game);
    }
    /* 5f07:132e-137f: the loop ends with DS:b7a7 set: it is cleared, each member freed
     * (177e:1e31: var 0x20 is their count, cleared as CAMP_LOAD does), and a load asked
     * (DS:acc4, 5f07:000f) loads here (604b:2e87), before main takes the camp from main
     * (5ebd:0312). Without one main goes on at 5ebd:039b: a new game (64b2:04ec) and main's
     * menu again (5ebd:022f), not ported, so the run stops there rather than go on with
     * no party. */
    queen *q = own(game);
    if (status == COK_ECL_OK && !vm->abort && q->b7a7) {
        q->b7a7 = false;
        vm->halt = false;
        cok_party_free(&game->party);
        vm->character = NULL;
        cok_ecl_store(vm, 0x20, 0);
        if (!q->load_asked) {
            cok_adventure_not_yet(game, "the party emptied in the adventure's camp: main's new "
                                        "game and menu", "dqk 5ebd:039b");
            return vm->status;
        }
        if (!load_game(game)) return vm->status;
    }
    return status != COK_ECL_OK ? status : vm->status;
}

/* eclplay's --play (cok_drawing.play): Begin Adventuring from a loaded game
 * as the camp takes it (begin_adventuring), then the adventure from block.
 * The camp's Begin is only on with a member, so a party is needed. */
static cok_ecl_status play(cok_adventure *game, uint8_t block)
{
    if (game->party.count == 0) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "--play: Begin Adventuring needs a party (add --character)");
        return game->vm.status;
    }
    begin_adventuring(game);
    return adventure(game, block, true);
}

/* The demonstration (5ebd:0236, 5f07:10cd with DS:ae59): the speed
 * (variable 0x12) 5, block 7's start run (its fifth vector) without the
 * camp or the copy protection, the keys read only where typed (2093:0078);
 * Escape or '`' ends it (DS:a890). Returns the run's status; *stopped
 * whether a key ended it. */
static cok_ecl_status demonstration(cok_adventure *game, bool *stopped)
{
    cok_ecl *vm = &game->vm;
    game->demo = true;
    cok_ecl_store(vm, 0x12, 5);
    cok_ecl_status status = cok_adventure_enter(game, 7);
    *stopped = vm->halt;
    vm->halt = false;
    game->demo = false;
    return status;
}

/* main (5ebd:00ef): the title (6523:0345), the frame's colours (FRAME 41),
 * the mouse's cursor shown (22b7:2122: on wherever a driver answers,
 * 22b7:1be3, at pixel 8, 8), main's menu; Play the camp's menu, Demo the
 * demonstration, then the title again (unless a key ended it) and the
 * menu with its shorter time; Quit to DOS (5ebd:0482). */
static cok_ecl_status start(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    mouse_start(game);
    if (!title(game)) return vm->status;
    frame_palette_of(game, 0);
    int choice = main_menu(game, 7000);
    for (;;) {
        if (choice < 0) return COK_ECL_OK;
        if (choice == 2) {
            cok_adventure_quit(game);
            return COK_ECL_OK;
        }
        if (choice != 1) {
            bool begin;
            cok_ecl_status status = camp(game, true, &begin);
            for (bool first = true; status == COK_ECL_OK && begin && !vm->abort; first = false) {
                status = adventure(game, (uint8_t)cok_ecl_load_var(vm, 0x16), first);
                if (status != COK_ECL_OK || vm->abort || !own(game)->load_asked) return status;
                /* 5ebd:031e-033a: the mode the last (DS:ae67), the request cleared (DS:acc4
                 * by 5f07:0004 with 0, DS:b7a7 with it: the run is not halted again), then
                 * the camp from main (1d99:1175 with 1) and the game from its block (10cd). */
                vm->mode = vm->last_mode;
                own(game)->load_asked = own(game)->b7a7 = false;
                vm->halt = false;
                status = camp(game, true, &begin);
            }
            return status;
        }
        bool stopped;
        cok_ecl_status status = demonstration(game, &stopped);
        if (status != COK_ECL_OK || vm->abort) return status;
        if (!stopped && !title(game)) return vm->status;
        frame_palette_of(game, 0);
        choice = main_menu(game, 0x4b0);
    }
}

/* What the other Dark Queen files use (queen.h). */

int queen_read_input(cok_adventure *game, queen_input *in)
{
    return read_input(game, in);
}

bool queen_hotkey(int h, int key)
{
    return hotkey(h, key);
}

bool queen_inside(const queen_rect *r, int x, int y)
{
    return inside(r, x, y);
}

void queen_beep(cok_adventure *game)
{
    beep(game);
}

void queen_controls_clear(cok_adventure *game)
{
    controls_clear(game);
}

void queen_control_add(cok_adventure *game, queen_rect r)
{
    control_add(game, r);
}

void queen_text(cok_adventure *game, int row, int col, const char *s, unsigned attribute)
{
    text(game, row, col, s, attribute);
}

void queen_say(cok_adventure *game, int x, int y, const char *s, unsigned fg)
{
    say(game, x, y, s, fg);
}

void queen_fill(cok_adventure *game, int y0, int x0, int y1, int x1, uint8_t colour)
{
    fill(game, y0, x0, y1, x1, colour);
}

void queen_clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2)
{
    clear_cells(game, x1, y1, x2, y2);
}

void queen_full_window(cok_adventure *game)
{
    full_window(game);
}

void queen_bottom_bar(cok_adventure *game)
{
    bottom_bar(game);
}

void queen_button(cok_adventure *game, int row, int col, const char *s, int width,
                  unsigned flags)
{
    button(game, row, col, s, width, flags);
}

queen_rect queen_button_rect(int row, int col, int width)
{
    return button_rect(row, col, width);
}

void queen_prompt_box(cok_adventure *game, int x, const char *s, unsigned fg, unsigned bg)
{
    prompt_box(game, x, s, fg, bg);
}

int queen_bar_menu(cok_adventure *game, const char *prompt, const char *const *items,
                   size_t count, bool dirs)
{
    return bar_menu(game, prompt, items, count, dirs, false, 0, 0);
}

int queen_yes_no(cok_adventure *game, const char *prompt)
{
    return yes_no(game, prompt, false);
}

int queen_edit_line(cok_adventure *game, char *line, size_t most)
{
    return edit_line(game, line, most);
}

void queen_draw_frame(cok_adventure *game, unsigned id, int row, int col)
{
    draw_frame_record(game, id, row, col, NULL);
}

void queen_draw_always(cok_adventure *game, unsigned id, int row, int col)
{
    draw(game, lib(game, LIB_ALWAYS), id, row, col, NULL, NULL);
}

void queen_draw_library(cok_adventure *game, unsigned which, unsigned id, int row, int col)
{
    draw(game, lib(game, which), id, row, col, NULL, NULL);
}

bool queen_set_palette(cok_adventure *game, unsigned which, unsigned set, unsigned record)
{
    if (set == 0) {
        set_palette(game, lib(game, which), record, 0);
        return true;
    }
    hlib_library sub;
    if (!sub_library(game, which, set, &sub)) return false;
    set_palette(game, &sub, record, 0);
    return true;
}

int queen_input_line(cok_adventure *game, const char *prompt, unsigned fg, unsigned bg,
                     char *line, size_t most)
{
    queen *q = own(game);
    bool shown = q->cursor_shown;
    q->cursor_shown = false;
    int result = read_line_prompt(game, prompt, fg, bg, line, most);
    q->cursor_shown = shown;
    return result;
}

bool queen_mouse(const cok_adventure *game)
{
    return game->mouse.driver.installed;
}

bool queen_write_character(cok_adventure *game, const cok_character *character)
{
    return write_character(game, character);
}

void queen_bar_message_colour(cok_adventure *game, const char *s, unsigned fg)
{
    bar_message_colour(game, s, fg);
}

bool queen_train_anywhere(const cok_adventure *game)
{
    return ((const queen *)game->own)->train_anywhere;
}

void queen_set_train_anywhere(cok_adventure *game, bool on)
{
    own(game)->train_anywhere = on;
}

int queen_bar_menu_areas(cok_adventure *game, const char *prompt, const char *const *items,
                        size_t count, bool dirs, const queen_rect *areas, size_t area_count)
{
    return bar_menu_areas(game, prompt, items, count, dirs, false, 0, 0, areas, NULL, area_count,
                          -1, false);
}

/* 1bdb:1595's mouse areas (15de-1828: 2aa6:1294's records in 2-px units, read as pixels here), in
 * the control order (the first added wins): the cursor and the code of each (1206's table at 126e:
 * hit - buttons - 1). P and Q are 24 px times the combatant's row and column on the view (0cd7's
 * bytes); test_dark_queen.c has the areas' edges for P, Q = 3, 4 and 0, 0. 1595 adds them, and the
 * type-7 control at 1810 (message 0x20, Space), only to a fresh control list. DS:a6b5 there is the
 * rebuilt flag 1bdb:0d26 leaves (its tail, e27-e2d, copies DS:1d13, which 0d1d or the bar's new text
 * sets): a list not rebuilt keeps its areas and its Space control. So Space is taken either way, and
 * the bar's filter (space = true) stands for the control. The port rebuilds the list on every call,
 * so it adds the areas every time. The mouse.on gate below is the port's, not the original's: with no
 * mouse no click arrives, so the areas cannot be seen without it. */
static const uint8_t combat_cursors[8] = {9, 11, 5, 7, 10, 4, 6, 8};
static const int combat_codes[8] = {0x86, 0x88, 0x82, 0x84, 0x87, 0x81, 0x83, 0x85};

int queen_combat_menu(cok_adventure *game, const char *const *items, size_t count, int split,
                      int p, int q)
{
    if (!game->mouse.on)
        return bar_menu_areas(game, "", items, count, true, false, 0, 0, NULL, NULL, 0, split,
                              true);
    int P = 24 * (uint8_t)p, Q = 24 * (uint8_t)q;
    const rect areas[8] = {
        {P + 8, 0, P + 32, Q + 8},
        {0, Q + 8, P + 4, Q + 32},
        {P + 8, Q + 28, P + 32, 184},
        {P + 36, Q + 4, 188, Q + 28},
        {0, 0, P + 8, Q + 8},
        {0, Q + 32, P + 8, 184},
        {P + 32, Q + 32, 184, 184},
        {P + 32, 0, 184, Q + 8},
    };
    int result = bar_menu_areas(game, "", items, count, true, false, 0, 0, areas, combat_cursors,
                                8, split, true);
    return result >= AREA_MENU ? combat_codes[result - AREA_MENU] : result;
}

void queen_combat_menu_close(cok_adventure *game)
{
    bottom_bar(game);
    draw_frame_record(game, 6, 0, 0, NULL);
}

/* Speed (dqk 5045:166c, from 08cf's 0b22): "game speed: (N) " on the bar over Faster (while the
 * speed is above 0), Slower (below 9) and Exit, by 1bdb:1e29 with no extended keys; Faster lowers
 * var 0x12 (and game->speed) by one, Slower raises it, and the menu is shown again after each.
 * Exit, or Escape or '`' (the bar's 0x1b), leaves it: true. False where the keys ran out. */
bool queen_speed(cok_adventure *game)
{
    for (;;) {
        unsigned speed = cok_ecl_var(&game->vm, COK_VAR_SPEED);
        char prompt[48];
        snprintf(prompt, sizeof prompt, "game speed: (%u) ", speed);
        const char *items[3];
        int index[3], n = 0;
        if (speed > 0) {
            index[n] = 0;
            items[n++] = "Faster";
        }
        if (speed < 9) {
            index[n] = 1;
            items[n++] = "Slower";
        }
        index[n] = 2;
        items[n++] = "Exit";
        int choice = bar_menu(game, prompt, items, (size_t)n, false, false, 0, 0);
        if (choice < 0) return false;
        if (choice == 0x1b) return true;
        if (choice >= n) continue;
        if (index[choice] == 2) return true;
        if (index[choice] == 0) set_speed(game, speed - 1u);
        else set_speed(game, speed + 1u);
    }
}

void queen_message(cok_adventure *game, const char *s, int x, unsigned fg, unsigned bg)
{
    message_at(game, s, x, fg, bg);
}

void queen_sheet_score(cok_adventure *game, const uint8_t *c, int k, bool lit)
{
    sheet_score(game, c, k, lit);
}

void queen_xor(cok_adventure *game, int y0, int x0, int y1, int x1, uint8_t mask)
{
    cok_picture *screen = &game->screen;
    if (!screen->vga) return;
    int sw = (int)screen->units * 8, sh = screen->height;
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < sh; ++y)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < sw; ++x)
            screen->pixels[(size_t)y * (size_t)sw + (size_t)x] ^= mask;
}

void queen_cursor_show(cok_adventure *game, bool shown)
{
    own(game)->cursor_shown = shown;
}

/* 86b8:065e: the character recomputed (177e:0be2), its movement again
 * (177e:0a5f), and the sheet's lower block drawn. */
bool queen_sheet_derived(cok_adventure *game, cok_character *character)
{
    char error[300];
    const cok_rules *rules = cok_game_rules(game);
    if (!cok_character_stats(character, rules, &game->item_types, error, sizeof error) ||
        !cok_character_movement_now(character, rules, &game->item_types, error,
                                    sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    sheet_derived(game, character->record);
    return true;
}

/* The sheet (86b8:0000) of a member or a character in the making: it
 * recomputes the character first (86b8:065e, 177e:0be2), and its
 * movement again (177e:0a5f), as the original does at the end of
 * drawing it. */
bool queen_sheet(cok_adventure *game, cok_character *character)
{
    char error[300];
    const cok_rules *rules = cok_game_rules(game);
    if (!cok_character_stats(character, rules, &game->item_types, error, sizeof error) ||
        !cok_character_movement_now(character, rules, &game->item_types, error,
                                    sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    sheet(game, character);
    return true;
}

/* The combat ground (queen_arena.c). arena_tiles is the battle's set and
 * its palette: the set is DUNGCOM's or WILDCOM's record `set` (59c7:0e70
 * loads DS:ca03 of "DungCom1" when var 0x22 is not 0, else of "WildCom1";
 * combat.c's build calls it in place of the 4-bit tiles, 59c7:1dd7 -> 0e70),
 * and its record 0 is applied as 204c:0007 applies it (19a4:0b8c, 2796:0536:
 * the colours, and the cycles started from the seed, 261b:00b1). The view
 * and the cells are drawn from the map (5837:06e6, 172c). */
static bool arena_tiles(cok_adventure *game, bool wild, unsigned set)
{
    queen *q = own(game);
    /* The missiles' and cursor's icons first (59c7:0e80-0f06): COMSPR records k - 15 and
     * k - 15 + 0x80 into slots k = 0x0f-0x25, no fallback (flag 1). The free at the battle's
     * end (5045:0001) is not reached by the port. */
    for (unsigned k = 0x0f; k <= 0x25; ++k)
        if (!icon_pair(game, LIB_COMSPR, k - 15, k - 15 + 0x80, k)) return false;
    if (!sub_library(game, wild ? LIB_WILDCOM : LIB_DUNGCOM, set, &q->arena)) return false;
    set_palette(game, &q->arena, 0, 0);
    q->text_delay = false; /* 59c7:1de2: the battle's setup */
    return true;
}

static bool arena_ready(cok_adventure *game)
{
    if (own(game)->arena.data != NULL) return true;
    snprintf(game->error, sizeof game->error, "no battlefield's tile set (arena_tiles)");
    return false;
}

static bool arena_view(cok_adventure *game)
{
    return arena_ready(game) &&
           cok_queen_arena_view(&game->screen, &own(game)->arena, cok_game_rules(game),
                                &game->combat, game->error, sizeof game->error);
}

static bool arena_cell(cok_adventure *game, int x, int y)
{
    return arena_ready(game) &&
           cok_queen_arena_cell(&game->screen, &own(game)->arena, cok_game_rules(game),
                                &game->combat, x, y, game->error, sizeof game->error);
}

/* A monster's icon, the monsters' loads (204c:00df with DS:778c, "CPIC": 29fc:2b0b and
 * 170e:17d3, monster.c's load_icons): the name is the rules' archives.icons. Any other name
 * stops, cited. The party's icons are loaded by member_icon, not here. */
static bool arena_load_icon(cok_adventure *game, const char *name, uint8_t id, uint8_t slot)
{
    if (strcmp(name, cok_game_rules(game)->archives.icons) != 0) {
        snprintf(game->error, sizeof game->error, "%s is not the monsters' icons (204c:00df)",
                 name);
        return false;
    }
    return icon_load(game, LIB_CPIC, id, slot);
}

/* A combatant's icon (dqk 204c:030f, from 5837's callers: the cursor's box, the occupant, the
 * pose and the redraw's cells), drawn on the view at x, y: the table's entry slot + 0x26 *
 * image (030f:0359-0366; image 0 ready, 1 attacking). An empty entry draws nothing. The head
 * overlay of 030f:03a2 and 0431 is dead here: DS:a848 is 0 (no CHEAD, 204c:00e5-0157). The
 * redraw's frame (51c2:0c94 -> 20b2:02fa: the panel and the borders) is drawn by the frame
 * hook (combat_screen), not by this. */
static bool arena_icon(cok_adventure *game, int8_t x, int8_t y, uint8_t facing, uint8_t image,
                       uint8_t slot)
{
    const uint8_t *record;
    size_t size;
    if (image > 1 || slot >= ICON_SLOTS) {
        snprintf(game->error, sizeof game->error,
                 "icon %u of slot %u is past the icons (DS:b17d, 030f:0359)", image, slot);
        return false;
    }
    if (!queen_icon(game, slot, image, &record, &size)) return false;
    return cok_queen_arena_icon(&game->screen, record, size, x, y, facing, game->error,
                                sizeof game->error);
}

const cok_drawing cok_dark_queen_drawing = {
    .open = queen_open,
    .close = queen_close,
    .frame = frame,
    .view = view,
    .opcode = opcode,
    .value = value,
    .stored = stored,
    .start = start,
    .animates = animates,
    .animate = animate_menu,
    .cursor = cursor,
    .party = party_hook,
    .status = status_line,
    .window_text = window_text,
    .window_clear = window_clear,
    .pick_bar = pick_bar,
    .load = load_saved,
    .play = play,
    .arena_tiles = arena_tiles,
    .arena_view = arena_view,
    .arena_cell = arena_cell,
    .arena_icon = arena_icon,
    .arena_load_icon = arena_load_icon,
    .arena_panel = combat_panel,
    .arena_say = arena_say,
    .arena_flash = arena_flash,
    .arena_text = arena_text,
    .arena_clear_text = arena_clear_text,
    .wait = cok_dark_queen_wait,
    .pause = battle_pause,
    .clear_bar = queen_bottom_bar,
    .notice = queen_bar_message,
    .encounter = encounter,
    .round_clock = cok_dark_queen_round_clock,
    .teammate_dying = cok_dark_queen_teammate_dying,
    .continue_battle = cok_dark_queen_continue_battle,
    .battle_icons = cok_dark_queen_battle_icons,
    .turn_tail_cycles = cok_dark_queen_turn_tail_cycles,
};

/* Heal (dqk 7825:0615): the selected member's name on row 19 and its hit points and maximum
 * (the name in light cyan on row 1 with ", how can we help you?"), then the list of eleven cures
 * (list_menu: 1bdb:1f25, cells 2-38 by 3-15) with "Heal" (the current cure) and "Exit" (or
 * Escape), redrawn after each cure until Exit (177e:1ad1). The cures are DS:82a4's spell ids in
 * order. Stone to Flesh (0524) is ported; every other cure stops, cited with its routine and
 * what it needs: its effects go through 148f (part 27), its spell through 849a or 7fd8. */
enum { HEAL_CURES = 11 };
static bool stone_to_flesh(cok_adventure *game, cok_character *who);

static bool heal(cok_adventure *game)
{
    static const char *const bar[] = {"Heal", "Exit"};
    static const uint8_t ids[HEAL_CURES] = {0x25, 0x27, 0x03, 0x3a, 0x47, 0x24, 0x43, 0x4b, 0x2b,
                                            0x71, 0x66};
    static const char *const stops[HEAL_CURES] = {
        "dqk 7825:0045 (177e:1734, 148f:017d)",
        "dqk 7825:00b8 (148f:017d, 148f:1dfc)",
        "dqk 7825:0193 (148f:1968, 148f:25f7, 148f:017d, 148f:1dfc)",
        "dqk 7825:0193 (148f:1968, 148f:25f7, 148f:017d, 148f:1dfc)",
        "dqk 7825:0193 (148f:1968, 148f:25f7, 148f:017d, 148f:1dfc)",
        "dqk 7825:0193 (148f:1968, 148f:25f7, 148f:017d, 148f:1dfc)",
        "dqk 7825:0394 (148f:017d)",
        "dqk 7825:02c0 (849a:21b5)",
        "dqk 7825:0479 (7fd8:13ef)",
        NULL, /* Stone to Flesh (0524): stone_to_flesh */
        "dqk 7825:059e (849a:21b5)",
    };
    const cok_rules *R = cok_game_rules(game);
    const cok_character_layout *L = R->character;
    const char *items[HEAL_CURES];
    for (size_t i = 0; i < HEAL_CURES; ++i) {
        items[i] = cok_spell_name(R, ids[i]);
        if (items[i] == NULL) return cok_adventure_not_yet(game, "a cure", "dqk 7825:0615");
    }
    bottom_bar(game);      /* 7825:0615: 1bdb:036c, then the overland's frame (10b2:02d3) */
    overland_screen(game); /* once, before the loop: the list and the member's lines on it */
    size_t at = 0;
    for (;;) {
        size_t i = cok_party_index(&game->party, game->vm.character);
        if (i >= game->party.count) return cok_adventure_not_yet(game, "Heal with no member", "dqk 7825:0615");
        cok_character *who = game->party.members[i];
        uint8_t *c = who->record;
        char name[16], now[8], most[8], line[48];
        clear_cells(game, 1, 0x13, 0x26, 0x13);
        cok_field_name(L, c, name);
        text(game, 8 * 0x13, 8 * 1, name, 0x8b);
        unsigned hp = cok_field_u8(L, c, COK_FIELD_HIT_POINTS);
        unsigned max = cok_field_u8(L, c, COK_FIELD_MAX_HIT_POINTS);
        int n = snprintf(now, sizeof now, "%u", hp);
        /* 7825:0737-074b: the colour byte is 0x0b at full hit points, else 0x00 (black). The
         * drawing (1a62:0046) puts it on the window's grey (bg 8) above row 0x17, as say does:
         * 0x80 | fg: the screen agrees at 0 pixels (0x00 with a black background differs by 121). */
        text(game, 8 * 0x13, 8 * 0x12, now, 0x80 | (hp == max ? 0x0b : 0x00));
        snprintf(most, sizeof most, "/%u", max);
        text(game, 8 * 0x13, 8 * (0x12 + n), most, 0x8b);
        snprintf(line, sizeof line, "%s, how can we help you?", name);
        text(game, 8, 8, line, 0x8b);
        int result = list_menu(game, "", bar, 2, items, HEAL_CURES, &at, 2, 3, 0x0f, 0x26);
        if (result < 0) return false;
        if (result == 1 || result == 0x1b) break;
        if (stops[at] == NULL) {
            if (!stone_to_flesh(game, who)) return false;
        } else {
            return cok_adventure_not_yet(game, items[at], stops[at]);
        }
    }
    cok_adventure_redraw(game); /* 7825:0615's end: 177e:1ad1, then the party's list */
    return true;
}

/* Stone to Flesh (dqk 7825:0524): a member not stoned (status not 7) is asked "cast cure anyway? "
 * after "is not stoned." (0001: 177e:156e, 1bdb:0424), and nothing changes whatever the answer.
 * A stoned member with a readied item of type 0x8c (+0x37, 0x32 set; 177e:1389, queen_readied_8c)
 * is told "spell dosn't work!" (no name line) and stays stoned. Otherwise its status is 0, it is active and has
 * 1 hit point. No money is taken. False where the keys ran out. */
static bool stone_to_flesh(cok_adventure *game, cok_character *who)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *c = who->record;
    if (cok_field_u8(L, c, COK_FIELD_STATUS) != 7) {
        member_say(game, c, "is not stoned.");
        int answer = yes_no(game, "cast cure anyway? ", false);
        clear_cells(game, 1, 0x12, 0x26, 0x16); /* 0001's 177e:1665 (mode 1): rows 18-22 */
        return answer >= 0;
    }
    if (queen_readied_8c(who)) {
        member_say(game, NULL, "spell dosn't work!");
        return true;
    }
    cok_field_set8(L, c, COK_FIELD_STATUS, 0);
    c[0x65] = 0; /* 7825:0588's movw: the status is a word, +0x64 and +0x65 */
    cok_field_set8(L, c, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, c, COK_FIELD_HIT_POINTS, 1);
    return true;
}

/* Whether a load is asked (DS:b7a7): the overland's mark after a fight is not made then
 * (5f07:0974), which combat's descriptor reads through this. */
bool cok_dark_queen_load_asked(cok_adventure *game)
{
    return own(game)->b7a7;
}

/* 75cc:0593's loops 1 and 3 stop at a record past the party: its combat record's not_party 1
 * (cok_character_combat's zero block with none). The original reads that byte at 0000:0014 when
 * the record has no combat block, as the end of combat with no monsters finds (every record's
 * is NULL, measured): the IVT byte, 0x54, is not 1, so the loops take every record. */
static bool past_party(const cok_character *c)
{
    return cok_character_combat(c)->not_party == 1;
}

/* The word at a one-byte field and the next byte: the status and hostile that 0593's cmpw tests
 * (5b7-6ba, 680), not the byte alone. */
static uint16_t word_field(const cok_character_layout *L, const uint8_t *r, cok_field f)
{
    size_t at = cok_field_offset(L, f);
    return (uint16_t)(r[at] | r[at + 1] << 8);
}

/* 10d7:2a72, Borland C's LDIV@ (signed): the magnitudes by 32 shifts and subtractions, then
 * the sign of the result (bit 4 of di). It has no division instruction and no test for 0, so a
 * divisor of 0 gives 0xffffffff, or 1 for a negative dividend; 0x80000000 / -1 is 0x80000000. */
static uint32_t ldiv_borland(uint32_t a, uint32_t b)
{
    bool negative = false;
    if (a & 0x80000000u) { a = 0u - a; negative = true; }
    if (b & 0x80000000u) { b = 0u - b; negative = !negative; }
    uint32_t q = b == 0 ? 0xffffffffu : a / b;
    return negative ? 0u - q : q;
}

/* 75cc:01fb, the experience (after 0593 when a member stands): every monster in the list
 * (hostile word 1, status word neither 0, 3 nor 9) sets DS:c7df, and one past the party (its
 * combat record's not_party 1) puts its coins (three words, steel, gems, jewelry) in the pool
 * and its experience (+0x54) in the sum. The pool's steel counts 2, its gems 250 and its
 * jewelry 2200, and each magic item with a bonus above 0 adds 400 times it (as a signed word,
 * 0x30; the last missile recovered, DS:c2e3, stops the walk). The sum, a LongInt, is divided by
 * the party's variable 0x20 less those out (DS:b1da, decremented first where the variable is no
 * more: 01fb 3a2-3ac), by ldiv_borland (2a72: a divisor of 0 gives 0xffffffff, not an error).
 * The share is DS:ae4f. A monster's items go to the pool through 02cb (part 19b): cited, the
 * first time one would move. The party with no monsters past it adds nothing here. */
static bool experience(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    const cok_rules *R = cok_game_rules(game);
    const cok_character_layout *L = R->character;
    const cok_rules_item_record *ir = &R->item_record;
    uint32_t total = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *c = game->party.members[i];
        uint8_t *r = cok_party_record(&game->party, i);
        unsigned status = word_field(L, r, COK_FIELD_STATUS);
        if (word_field(L, r, COK_FIELD_HOSTILE) != 1 || status == 0 || status == 3 ||
            status == 9)
            continue;
        q->monsters_present = true; /* 254: DS:c7df */
        if (!past_party(c)) continue; /* 25d: the combat record's not_party 1 */
        size_t first = cok_field_first(L, COK_FIELD_COINS);
        for (size_t k = first; k < first + cok_field_count(L, COK_FIELD_COINS); ++k)
            game->pool.coins[k] += cok_field_u16_at(L, r, COK_FIELD_COINS, k);
        total += cok_field_u16(L, r, COK_FIELD_MONSTER_EXPERIENCE);
        if (cok_ecl_load_var(vm, 0x1e) == 1) continue; /* KEEP_ITEMS: the items stay (2a5) */
        if (c->item_count != 0 && c->items[0][ir->type] != 0x69) {
            /* 2b3-2be: the first item (+0x28) is not 'i'; the items move to the pool (2cb-338,
             * with 000b, the merge of counted items, for each: part 19b). */
            cok_adventure_not_yet(game, "the treasure", "dqk 75cc:02cb (a monster's items to the "
                                                       "pool; 000b merges counted items, part "
                                                       "19b)");
            return false;
        }
    }
    const uint32_t *coins = game->pool.coins;
    total += coins[4] * 2u + coins[5] * 250u + coins[6] * 2200u; /* 35b-397 */
    for (size_t i = 0; i < game->pool.item_count && i + 1 != game->pool.missile; ++i) {
        int8_t bonus = (int8_t)game->pool.items[i][ir->bonus];
        if (bonus > 0) { /* 3a6-3c1: the product is a signed word (cwtd after mul) */
            int16_t product = (int16_t)(uint16_t)(bonus * 400);
            total = (uint32_t)((int32_t)total + (int32_t)product);
        }
    }
    uint8_t var = (uint8_t)cok_ecl_load_var(vm, 0x20);
    if (var <= q->out) --q->out; /* 3e7-3f5: DS:b1da */
    int32_t divisor = (int32_t)(int16_t)((int)var - (int)q->out); /* 3f9-40c */
    q->share = ldiv_borland(total, (uint32_t)divisor); /* 414-41c: DS:ae4f */
    return true;
}

/* 75cc:0427, the award (after 01fb, when a member stands): unless the bank pays out (var 0x1f),
 * every party record active (+0x18b) and not animated (status word 1) gets the share, by its
 * class (+0x5a word): 10% more for a cleric of wisdom above 14, or a fighter (2), paladin (3),
 * ranger (4), mage (5) or thief (6) whose prime requisites are above 15 (the same as Champions'
 * award); half for classes 8, 10-14 and 16; a third for 9 and 15 (the switch at 0427's 481).
 * Its experience (+0x44) grows by that, then the record's may-train (177e:1fe5) is set (+0xcd). */
static bool award(cok_adventure *game)
{
    const cok_rules *R = cok_game_rules(game);
    const cok_character_layout *L = R->character;
    if (cok_ecl_load_var(&game->vm, 0x1f) != 0) return true; /* 42d */
    uint32_t share = own(game)->share;
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *r = cok_party_record(&game->party, i);
        if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0 || word_field(L, r, COK_FIELD_STATUS) == 1)
            continue;
        unsigned klass = word_field(L, r, COK_FIELD_CLASS);
        uint8_t str = cok_field_u8_at(L, r, COK_FIELD_SCORE, COK_SCORE_STRENGTH),
                intel = cok_field_u8_at(L, r, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE),
                wis = cok_field_u8_at(L, r, COK_FIELD_SCORE, COK_SCORE_WISDOM),
                dex = cok_field_u8_at(L, r, COK_FIELD_SCORE, COK_SCORE_DEXTERITY);
        bool bonus = (klass == 0 && wis > 14) || (klass == 2 && str > 15) ||
                     (klass == 3 && str > 15 && wis > 15) ||
                     (klass == 4 && str > 15 && intel > 15 && wis > 15) ||
                     (klass == 5 && intel > 15) || (klass == 6 && dex > 15);
        uint32_t gain = share;
        if (bonus)
            gain = ldiv_borland(share, 10) + share; /* 4b0 */
        else if (klass == 8 || (klass >= 10 && klass <= 14) || klass == 16)
            gain = ldiv_borland(share, 2); /* 522 */
        else if (klass == 9 || klass == 15)
            gain = ldiv_borland(share, 3); /* 53c */
        uint32_t experience_now = cok_field_u32(L, r, COK_FIELD_EXPERIENCE) + gain; /* 557-561 */
        cok_field_set32(L, r, COK_FIELD_EXPERIENCE, experience_now);
        bool may;
        if (!cok_queen_may_train(R, r, &may, game->error, sizeof game->error)) {
            /* A knight of no order: 177e:1f39 returns its row word unset, a stack word that the
             * port does not model (the previous call's word at that depth). Knights are made
             * with order 1 (queen_create.c). */
            cok_adventure_not_yet(game, "a knight of no order's may-train (177e:1fe5)",
                                  "dqk 177e:1f39 (the row is unset: a stack word, part 19c)");
            return false;
        }
        cok_field_set8(L, r, COK_FIELD_MAY_TRAIN, may); /* 56c-573 */
    }
    return true;
}

/* 177e:1e31 with (0, 1): the member at index i removed, as 7d8 and 839 do it: the selection is
 * the member at i first (ad67, 7d8-7df), then 1e31 leaves it on the one before (1f07-1f0e), or
 * the next where it was the first (1f20-1f31). remove_selected does that: its icons go (1e9d's
 * flag, 204c:004d), its record is freed (1c87, cok_party_remove), and the party's size (var 0x20)
 * is one less (1f16, the first argument 0). 11b0, when it is ported, ends with the selection on
 * the first record (1287), whatever this leaves. */
static void remove_member(cok_adventure *game, size_t i)
{
    game->vm.character = cok_party_record(&game->party, i);
    remove_selected(game, true);
}

/* 7bb and 777's revive: status 0 and active (+0x18b); 777 also sets the hit points (+0x198). */
static void revive(const cok_character_layout *L, uint8_t *r, bool hit_points)
{
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    if (hit_points) cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 1);
}

/* 75cc:11b0, after 0593 (135c): each monster (its combat record's not_party 1, or hostile word
 * 1) sets DS:c7df, counts in variable 0x15 (DROPPED) when it is not active (+0x18b), and is
 * removed (177e:1e31 with the first argument its not_party test: the party's size is one less
 * only for a record in the party). Every other record loses its combat block (1df9:03c1 frees
 * the 0x18 bytes at +0x40; +0x40 and +0x42 are 0). The selection ends on the first record
 * (1287). Records are walked by index, a removal leaving the next record at the same index. */
static void enemies_removed(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_ecl_store(vm, 0x15, 0); /* 11ba */
    for (size_t i = 0; i < game->party.count;) {
        cok_character *c = game->party.members[i];
        uint8_t *r = cok_party_record(&game->party, i);
        bool not_party = cok_character_combat(c)->not_party == 1; /* 11d3 */
        if (not_party || word_field(L, r, COK_FIELD_HOSTILE) == 1) {
            q->monsters_present = true; /* 11e4: DS:c7df */
            if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0) /* 11f4: the byte at 0x15 + 1 */
                cok_ecl_store(vm, 0x15, (uint8_t)(cok_ecl_load_var(vm, 0x15) + 1));
            vm->character = r;                 /* 1208: DS:ad67 */
            remove_selected(game, !not_party); /* 1229: 1e31(not_party, 1) */
            continue;
        }
        free(c->combat); /* 123b-126a: the combat block back to its pool */
        c->combat = NULL;
        ++i;
    }
    vm->character = cok_party_record(&game->party, 0); /* 1287: DS:ad67 = the first record */
}

/* 75cc:0736 with no load asked (DS:b7a7 clear: a member stands), loop A: each record until one
 * past the party. With the fled flag (DS:c7de) one that is not status 3 is removed (7d8);
 * otherwise status 3 is revived (7bb), status 4 is revived when its hit points (+0x198) are above
 * 0, or else when variable 0x1d is set (777 also gives hit points; 0 stays 4), and status 5 is
 * revived with variable 0x1d, else it becomes 4 (78e). Other statuses stay as they are. */
static void standing_rest(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (size_t i = 0; i < game->party.count && !past_party(game->party.members[i]);) {
        uint8_t *r = cok_party_record(&game->party, i);
        unsigned status = word_field(L, r, COK_FIELD_STATUS);
        bool gentle = cok_ecl_load_var(vm, 0x1d) != 0;
        if (q->fled) {
            if (status != 3) {
                remove_member(game, i);
                continue;
            }
            revive(L, r, false);
        } else if (status == 3) {
            revive(L, r, false);
        } else if (status == 4) {
            if (cok_field_u8(L, r, COK_FIELD_HIT_POINTS) > 0) revive(L, r, false);
            else if (gentle) revive(L, r, true);
        } else if (status == 5) {
            if (gentle) revive(L, r, true);
            else cok_field_set8(L, r, COK_FIELD_STATUS, 4);
        }
        ++i;
    }
}

/* 75cc:0736 with the load asked (DS:b7a7 set: no member stands), 818-86e: every record until one
 * past the party is removed (828-854), then the party's size (var 0x20) is 0 (86e). */
static void destroyed_party(cok_adventure *game)
{
    for (size_t i = 0; i < game->party.count && !past_party(game->party.members[i]);)
        remove_member(game, i);
    cok_ecl_store(&game->vm, 0x20, 0);
}

/* 75cc:0593, the party's after combat (135c calls it unless the demonstration runs). It sets
 * b1da, b7a7, c7de and af3f from the records (loops 1 to 3, as the original's), removes the
 * effects that end with the battle (cok_dark_queen_end_effects: eighteen 017d calls for each record)
 * from each record, runs 01fb and 0427 (the experience and the award) when a member stands, then
 * its rest (0736: standing_rest or destroyed_party) and the exit (091b). The demonstration's branch (087a-091b, DS:ae59) is not run: 135c
 * calls 0593 only when it is clear (1385). False where the run stopped. */
static bool after_combat(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    size_t n = game->party.count;
    q->out = 0;                /* 599 */
    q->b7a7 = true;            /* 59e */
    q->fled = false;           /* 5a3 */
    for (size_t i = 0; i < n && !past_party(game->party.members[i]); ++i) { /* loop 1, 5a8 */
        uint8_t *r = cok_party_record(&game->party, i);
        if (word_field(L, r, COK_FIELD_STATUS) == 3) q->fled = true;
    }
    bool found = false; /* loop 2, 5e5: the first member out, to the first found */
    for (size_t i = 0; i < n && !found; ++i) {
        uint8_t *r = cok_party_record(&game->party, i);
        unsigned status = word_field(L, r, COK_FIELD_STATUS);
        found = cok_field_u8(L, r, COK_FIELD_ACTIVE) != 0 || status == 3 || status == 4 ||
                status == 5;
    }
    if (found && cok_ecl_load_var(vm, 0x1d) != 0) q->b7a7 = false; /* 633-644: var 0x1d */
    q->standing = false;       /* 649 */
    for (size_t i = 0; i < n && !past_party(game->party.members[i]); ++i) { /* loop 3, 665 */
        cok_character *c = game->party.members[i];
        uint8_t *r = cok_party_record(&game->party, i);
        unsigned status = word_field(L, r, COK_FIELD_STATUS);
        bool npc = (cok_field_u8(L, r, COK_FIELD_NPC) & 0x80) != 0;
        if ((status == 3 || status == 1 || status == 0) &&
            word_field(L, r, COK_FIELD_HOSTILE) == 0 && !npc)
            q->b7a7 = false; /* 67d-68f */
        if (status == 1 || status == 0) { /* 694-6aa */
            q->standing = true;
            q->fled = false;
        }
        if (cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0 || status == 1) ++q->out; /* 6af-6c1 */
        for (size_t k = 0; k < COK_DARK_QUEEN_END_EFFECTS; ++k) /* loop 3's 06cb: 017d */
            if (!cok_effects_remove(&game->effects, c, NULL, cok_dark_queen_end_effects[k]))
                return cok_adventure_effect_failed(game);
    }
    if (q->standing) { /* 0716-0731: 01fb, then 0427 (the award) */
        if (!experience(game) || !award(game)) return false;
    }
    if (q->b7a7) destroyed_party(game); /* 740 */
    else standing_rest(game);
    return true; /* 091b: the exit */
}

/* 74b9:092c: the pool's money (its coins 4-6, steel, gems and jewelry, not 0) and items
 * (the list not NULL). */
static void pool_flags(cok_adventure *game, bool *money, bool *items)
{
    *money = false;
    for (size_t k = 4; k < 7; ++k)
        if (game->pool.coins[k] != 0) *money = true;
    *items = game->pool.items != NULL;
}

/* 75cc:091f, the results (485 bytes), after 135c's share (DS:ae4f): the full window (20b2:0030),
 * then with monsters (DS:c7df), "the party has fled." (DS:c7de: the share 0, the pool's items freed
 * and its three coins cleared, 2093:0015 at DS:b1db), "you have lost the fight." (no member
 * standing, var 0x1d: the share 0), else "The party has won.". Then "each character receives N"
 * and "experience points." at rows 5 and 7 unless var 0x1f is set (N is the share, the buffer of
 * 2d1b:003a at 9fc is never read and not made), and "the party has found treasure" or "... no
 * treasure" at row 10 by 74b9:092c's flags (*money: the pool's coins 4-6; *items: its items).
 * Then the bar's "Press button or enter to continue" wait (1bdb:1e29, no items; the text's delay
 * is 1a62:0046's, none). False where the keys ran out. */
static bool results(cok_adventure *game, uint32_t share, bool *money, bool *items)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    full_window(game); /* 925 */
    if (q->monsters_present) { /* 92a: DS:c7df */
        if (q->fled) { /* 934: DS:c7de */
            say(game, 1, 3, "the party has fled.", 7);
            share = 0;
            cok_pool_free_items(&game->pool); /* 961-9a4: the items' nodes, already freed */
            memset(&game->pool.coins[4], 0, 3 * sizeof game->pool.coins[4]);
        } else if (!q->standing && cok_ecl_load_var(vm, 0x1d) != 0) { /* 9a9: DS:af3f */
            say(game, 1, 3, "you have lost the fight.", 7);
            share = 0;
        } else {
            say(game, 1, 3, "The party has won.", 7); /* 9e3 */
        }
    }
    char line[96];
    snprintf(line, sizeof line, "%s%ld", "each character receives ", (long)(int32_t)share);
    if (cok_ecl_load_var(vm, 0x1f) == 0) { /* a3e */
        say(game, 1, 5, line, 7);
        say(game, 1, 7, "experience points.", 7);
    }
    /* 74b9:092c: DS:c3e9 and c7e0 cleared, then set from the pool's coins 4-6 and its items. */
    pool_flags(game, money, items);
    if (*money || *items) say(game, 1, 10, "the party has found treasure", 7); /* a9a */
    else say(game, 1, 10, "the party has found no treasure", 7);
    /* ae2: 1bdb:1e29 with the empty list and the prompt; 1 and 0 as its last two arguments. */
    return bar_menu(game, "Press button or enter to continue", NULL, 0, true, false, 0, 0) >= 0;
}

/* 75cc:1460-14e8, the lost party (DS:b7a7 set at 13d6): the full window, then "The Dark Queen
 * sneers with pleasure" / "at your meaningless demise." / "Nothing can stop her now... NOTHING!"
 * in the window from cell 2 by rows 5-7 (the text's cursor at 2, 6 first), each word slow while
 * DS:ae65 is set (its delay draws random(1000) per character: 1a62:0311's 052a), then the
 * continue prompt (1bdb:0511: 1bdb:0495, then 2aa6:1586's wait for Return or the button). False
 * where the keys ran out. */
static bool lost_party(cok_adventure *game)
{
    full_window(game); /* 1460 */
    game->vm.cursor = (cok_text_cursor){2, 6}; /* 1465-146a: DS:ae4d, ae4e */
    if (!print_window(game, "The Dark Queen sneers with pleasure", 2, 5, 0x25, 0x16, 7, true))
        return false;
    if (!print_window(game, "at your meaningless demise.", 2, 6, 0x25, 0x16, 7, true))
        return false;
    if (!print_window(game, "Nothing can stop her now... NOTHING!", 2, 7, 0x25, 0x16, 7, true))
        return false;
    return continue_prompt(game, false, false); /* 093f -> 1bdb:0511 */
}

/* The treasure menu's Detect (75cc:0ed5-0f5e): the selected record (DS:ad67) has a
 * spell byte at +0xce+i (i 0-0x88) that is 5, 0x0b, 0x4d or 0x3c, and +0x18b (active)
 * is not 0. The spell is not cast here: Detect stops (849a:145b, part 28). */
static bool treasure_detect(const uint8_t *c)
{
    if (c == NULL || c[0x18b] == 0) return false;
    for (size_t i = 0; i <= 0x88; ++i) {
        uint8_t b = c[0xce + i];
        if (b == 5 || b == 0x0b || b == 0x4d || b == 0x3c) return true;
    }
    return false;
}

/* 10d7:2a7b, Borland C's LMOD@ (signed), with ldiv_borland's magnitudes and signs: the
 * remainder's sign is the dividend's, flipped for a negative divisor. A divisor of 0 gives the
 * dividend (no subtraction). */
static uint32_t lmod_borland(uint32_t a, uint32_t b)
{
    bool negative = (a & 0x80000000u) != 0, divisor_negative = (b & 0x80000000u) != 0;
    if (negative) a = 0u - a;
    if (divisor_negative) b = 0u - b;
    uint32_t r = b == 0 ? a : a % b;
    return negative != divisor_negative ? 0u - r : r;
}

/* The carry cap: cok_character_allowance + 1500 as a word (86b8:277c, 177e:1273 + 1500). */
static bool treasure_cap(cok_adventure *game, const uint8_t *c, uint16_t *cap)
{
    int16_t allowance;
    char error[300];
    if (!cok_character_allowance(c, cok_game_rules(game), &allowance, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    *cap = (uint16_t)((uint16_t)allowance + 0x5dc);
    return true;
}

/* 74b9:0000, the overload test: the weight (+0x56) and the amount as one 32-bit sum against the
 * cap. *fit is the room left when it is over and the weight is within the cap, else 0. Unlike
 * cok_pool_overloaded's 16-bit sum, a sum past 0xffff is over. False where the run stopped. */
static bool treasure_overloaded(cok_adventure *game, const uint8_t *c, uint32_t amount, bool *over,
                                uint16_t *fit)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint16_t cap;
    if (!treasure_cap(game, c, &cap)) return false;
    uint16_t weight = cok_field_u16(L, c, COK_FIELD_WEIGHT);
    *over = (uint32_t)weight + amount > cap;
    *fit = *over && weight <= cap ? (uint16_t)(cap - weight) : 0;
    return true;
}

/* The coin add of 74b9:0652-066a and 0324-0332 with 86b8:2799 (the weight add): a word of coin j
 * and the weight (+0x56) more, as words. Champions' 58e7:0053 is another copy of the shape. */
static void treasure_give(const cok_character_layout *L, uint8_t *c, size_t j, uint16_t amount)
{
    cok_field_set16_at(L, c, COK_FIELD_COINS, (int)j,
                       (uint16_t)(cok_field_u16_at(L, c, COK_FIELD_COINS, (int)j) + amount));
    cok_field_set16(L, c, COK_FIELD_WEIGHT,
                    (uint16_t)(cok_field_u16(L, c, COK_FIELD_WEIGHT) + amount));
}

/* The pool's coins 4-6 by name (74b9:0683's DS:3bd2: steel, gems, jewelry). */
static const char *const pool_coin_names[3] = {"Steel", "Gems", "Jewelry"};

/* 74b9:01ee, Share. n is the count of records with NPC 0 or 0xb3 (a byte, 74b9:01a1). Each coin
 * with a positive count gives each share (LDIV@, a word) and rest (LMOD@, a word); with n 0 the
 * share is 0xffff and the rest the count. Then each record with NPC below 0x80 (a 0xb3 record is
 * counted but takes nothing, a 0x01-0x7f one takes without being counted) takes its share, jewelry
 * down to steel, and one more from the rest while it fits. What is left goes to every record in turn,
 * as much as each has room for (the room wraps for one carrying more than it may). The pool keeps
 * what is left. False where the run stopped. */
static bool treasure_share(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    size_t first = cok_field_first(L, COK_FIELD_COINS);
    size_t end = first + cok_field_count(L, COK_FIELD_COINS);
    uint8_t n = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t kind = cok_field_u8(L, game->party.members[i]->record, COK_FIELD_NPC);
        if (kind == 0 || kind == 0xb3) ++n;
    }
    uint16_t share[COK_COINS] = {0}, rest[COK_COINS] = {0};
    for (size_t j = first; j < end; ++j) {
        int32_t coins = (int32_t)game->pool.coins[j];
        if (coins <= 0) continue;
        share[j] = (uint16_t)ldiv_borland((uint32_t)coins, n);
        rest[j] = (uint16_t)lmod_borland((uint32_t)coins, n);
    }
    for (size_t i = 0; i < game->party.count; ++i) {
        uint8_t *c = game->party.members[i]->record;
        if (cok_field_u8(L, c, COK_FIELD_NPC) >= 0x80) continue;
        for (size_t j = end; j-- > first;) {
            bool over;
            uint16_t fit;
            if (!treasure_overloaded(game, c, share[j], &over, &fit)) return false;
            if (over) {
                treasure_give(L, c, j, fit);
                rest[j] = (uint16_t)(rest[j] + share[j] - fit);
                continue;
            }
            treasure_give(L, c, j, share[j]);
            if (rest[j] == 0) continue;
            if (!treasure_overloaded(game, c, 1, &over, &fit)) return false;
            if (over) continue;
            treasure_give(L, c, j, 1);
            --rest[j];
        }
    }
    for (size_t j = end; j-- > first;) {
        if (rest[j] == 0) continue;
        for (size_t i = 0; i < game->party.count; ++i) {
            uint8_t *c = game->party.members[i]->record;
            uint16_t cap;
            if (!treasure_cap(game, c, &cap)) return false;
            uint16_t room = (uint16_t)(cap - cok_field_u16(L, c, COK_FIELD_WEIGHT));
            if (room == 0) continue;
            uint16_t amount = rest[j] > room ? room : rest[j];
            treasure_give(L, c, j, amount);
            rest[j] = (uint16_t)(rest[j] - amount);
        }
    }
    for (size_t j = first; j < end; ++j) game->pool.coins[j] = rest[j];
    return true;
}

/* 74b9:0683 with 74b9:05c0: the pool's coins (those with a positive count) as a list, Jewelry,
 * Gems, Steel (the list is built by prepending), under "select type of coin " with Select and
 * Exit. Select gives the coin of the row (*at kept across passes): "How many Gems will you take? "
 * or "How much ... will you take? ", the amount (queen_amount_input, past the pool's count it is
 * the count). Over (the overload test on the amount as a word) says "Overloaded" on the bar and
 * waits 100 ms times var 0x12, the keys are flushed, and nothing is taken. Otherwise the coin
 * moves from the pool to the selected member's coins and weight. Until no coin is left, or Exit
 * or Escape. False where the run stopped. */
static bool treasure_take_money(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    const cok_character_layout *L = cok_game_rules(game)->character;
    size_t first = cok_field_first(L, COK_FIELD_COINS);
    size_t end = first + cok_field_count(L, COK_FIELD_COINS);
    static const char *const bar[] = {"Select", "Exit"};
    full_window(game); /* 20b2:0030, once */
    size_t at = 0;
    for (;;) {
        char rows[3][32];
        const char *shown[3];
        size_t which[3], n = 0;
        for (size_t j = end; j-- > first;) {
            int32_t coins = (int32_t)game->pool.coins[j];
            if (coins <= 0) continue;
            snprintf(rows[n], sizeof rows[n], "%s %ld", pool_coin_names[j - first], (long)coins);
            shown[n] = rows[n];
            which[n++] = j;
        }
        if (n == 0) return true;
        /* A row taken whole can leave the cursor past the last row: 1f25's list (1b05:08b3 ->
         * 0055) keeps it to the last row, max(0, min(count - 1, at)); list_menu would reset it. */
        if (at >= n) at = n - 1;
        int picked = queen_list_menu(game, "select type of coin ", bar, 2, shown, n, &at, 2, 2, 8,
                                     0x12);
        if (picked < 0) return false;
        if (picked != 0) return true; /* Exit (1) or Escape */
        size_t j = which[at < n ? at : 0];
        char prompt[64];
        snprintf(prompt, sizeof prompt, "How %s %s will you take? ",
                 j == first + 1 ? "many" : "much", pool_coin_names[j - first]);
        long amount = queen_amount_input(game, prompt, (long)game->pool.coins[j]);
        if (amount < 0) return false;
        uint8_t *c = vm->character;
        bool over;
        uint16_t fit;
        if (!treasure_overloaded(game, c, (uint16_t)amount, &over, &fit)) return false;
        if (over) {
            queen_bar_message(game, "Overloaded");
            cok_dark_queen_wait(game, 100u * cok_ecl_load_var(vm, 0x12));
            cok_adventure_flush(game);
            continue;
        }
        uint32_t take = (uint16_t)amount;
        if ((int32_t)take > (int32_t)game->pool.coins[j]) take = game->pool.coins[j];
        game->pool.coins[j] -= take;
        treasure_give(L, c, j, (uint16_t)take);
    }
}

/* 75cc:0d01, Take, with the pool's money and items: with no money the items (0bbe, a stop); with
 * no items the money (0683) and the mode-6 screen; with both, the bar "Take:" (Money, Items, Exit)
 * again after each money pass (the items stay: only Items' stop leaves them). Escape or Exit
 * ends it. False where the run stopped. */
static bool treasure_take(cok_adventure *game)
{
    bool money, items;
    pool_flags(game, &money, &items);
    if (!money)
        return cok_adventure_not_yet(game, "Take's items", "dqk 75cc:0bbe (Take's items, part 21)");
    if (!items) {
        if (!treasure_take_money(game)) return false;
        return screen_as_it_was(game);
    }
    static const char *const words[] = {"Money", "Items", "Exit"};
    for (;;) {
        /* 0d28: "Money Items Exit"; with the money gone "Items Exit" (DS:7fda), index + 1. */
        log_menu(game, money ? words : words + 1, money ? 3 : 2);
        int choice = money ? bar_menu(game, "Take:", words, 3, true, true, 0, 0)
                           : bar_menu(game, "Take:", words + 1, 2, true, true, 0, 0);
        if (choice < 0) return false;
        if (!money && choice < 2) ++choice;
        if (choice == 0x1b || choice == 2) return true;
        if (choice == 0) {
            if (!treasure_take_money(game)) return false;
            if (!screen_as_it_was(game)) return false;
            pool_flags(game, &money, &items); /* 0e50: 092c */
        } else if (choice == 1) {
            return cok_adventure_not_yet(game, "Take's items",
                                         "dqk 75cc:0bbe (Take's items, part 21)");
        } else {
            member_key(game, choice); /* 0e15: every other code, End and Home too */
        }
    }
}

enum { TM_VIEW, TM_TAKE, TM_POOL, TM_SHARE, TM_DETECT, TM_EXIT, TM_ITEMS };
static const char *const treasure_words[TM_ITEMS] = {"View", "Take", "Pool", "Share", "Detect",
                                                     "Exit"};

/* 75cc:0e93, the treasure menu, after the results with money or items in the pool. The
 * screen is as it was in mode 6 (177e:1ad1, once). Each pass lists View; Take with money
 * or items; Pool; Share with money; Detect when treasure_detect finds a spell; Exit (the
 * jump table at 75cc:109b). View shows the sheet and the screen again. Exit with money or
 * items says "there is still treasure left." and asks whether to go back and claim it: Yes
 * clears the window and the menu comes again, No (or Escape) leaves. Take, Pool and Share are
 * treasure_take, cok_pool_gather and treasure_share; Take's items and Detect's cast stop, cited
 * (part 21; part 28). Escape is Exit; Up and Down pick a member (member_key, 5e22:067c);
 * End and Home do nothing (75cc:103e); other codes do nothing. DS:c6e4 (the saved item),
 * DS:ae5a (the pool's flag: no reader found) and the party list's mouse area (1bdb:0c00,
 * 0a33, DS:1d0f) are not modelled; with the mouse on a click outside the buttons beeps. The
 * ECL's song call (2023:0006) at the end is not played (as at PROGRAM's songs). False where
 * the run stopped; true when the pool may be freed. */
static bool treasure_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (!screen_as_it_was(game)) return false; /* 75cc:0e9d: 177e:1ad1, mode 6 */
    for (;;) {
        bool money, items;
        pool_flags(game, &money, &items); /* 75cc:0ea6 (74b9:092c) */
        bool detect = items && treasure_detect(vm->character);
        int shown[TM_ITEMS];
        size_t n = 0;
        shown[n++] = TM_VIEW;
        if (money || items) shown[n++] = TM_TAKE;
        shown[n++] = TM_POOL;
        if (money) shown[n++] = TM_SHARE;
        if (detect) shown[n++] = TM_DETECT;
        shown[n++] = TM_EXIT;
        const char *labels[TM_ITEMS];
        for (size_t i = 0; i < n; ++i) labels[i] = treasure_words[shown[i]];
        log_menu(game, labels, n);
        int choice = bar_menu(game, "", labels, n, true, true, 0, 0);
        if (choice < 0) return false;
        int code;
        if (choice == 0x1b) {
            code = TM_EXIT;
        } else if (choice == 0x84 || choice == 0x88) {
            member_key(game, choice); /* 75cc:1034, 1039: End and Home (0x85, 0x87) do nothing */
            continue;
        } else if (choice < (int)n) {
            code = shown[choice];
        } else {
            continue; /* other codes do nothing */
        }
        log_choice(game, code);
        switch (code) {
        case TM_VIEW:
            /* 10a7: 2d99:0fef (86b8:0b0c), then 177e:1ad1 (86b8:0d97) */
            if (!view_character(game, NULL)) return false;
            if (!screen_as_it_was(game)) return false;
            break;
        case TM_TAKE: /* 10b6: 75cc:0d01 */
            if (!treasure_take(game)) return false;
            break;
        case TM_POOL: /* 10ca: 74b9:00f6 (cok_pool_gather: the same routine) */
            cok_pool_gather(game);
            break;
        case TM_SHARE: /* 10d2: 74b9:01ee */
            if (!treasure_share(game)) return false;
            break;
        case TM_DETECT:
            return cok_adventure_not_yet(game, "Detect in the treasure menu", "dqk 849a:145b "
                                                                              "(Detect's "
                                                                              "cast, part 28)");
        case TM_EXIT: {
            pool_flags(game, &money, &items); /* 10f3: 092c again */
            if (!money && !items) return true;
            /* 1a62:0311 (cells 1-38 by 17-22, colour 7, cleared), then the question on
             * from the cursor (colour 0x0b, not cleared), then the Yes or No bar (0424). */
            if (!print_window(game, "there is still treasure left. ", 1, 0x11, 0x26, 0x16, 7,
                              true))
                return false;
            if (!print_window(game, "do you want to go back and claim it?", 1, 0x11, 0x26, 0x16,
                              0x0b, false))
                return false;
            int yes = yes_no(game, "", game->picture_shown); /* 1bdb:0424 animates by DS:ae64 */
            if (yes < 0) return false;
            if (yes == 0) return true;
            clear_cells(game, 1, 0x11, 0x26, 0x16); /* 1a62:09a1 */
            break;
        }
        default:
            break;
        }
    }
}

/* 75cc:135c after 11b0 (the mode 6 is set): 13ae sets flag 1 (<>), each record's stats are
 * recomputed (177e:0be2, cok_character_stats) and then: the lost party (DS:b7a7, 1460); else, when
 * fled (DS:c7de) the pool's items are freed (13e7-1425), otherwise flag 0 (=) is set and flag 1
 * cleared (1427); then the results (091f), and with the pool's money or items the treasure menu
 * (75cc:0e93, treasure_menu). The pool is emptied (63ee:0002), the vars 0x01, 0x2f, 0x1c,
 * 0x1e, 0x1d and 0x1f cleared (14eb-150d) and the monsters cleared (75cc:151b, monster.c's shared
 * clear). False where the run stopped. */
static bool treasure_end(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    const cok_rules *rules = cok_game_rules(game);
    vm->flags[1] = true; /* 13ae: DS:b2f6 */
    for (size_t i = 0; i < game->party.count; ++i) { /* 13b3-13d4: 177e:0be2 for each record */
        char error[300];
        if (!cok_character_stats(game->party.members[i], rules, &game->item_types, error,
                                 sizeof error)) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
            return false;
        }
    }
    if (q->b7a7) { /* 13d6 */
        if (!lost_party(game)) return false;
    } else {
        if (q->fled) { /* 13e0 */
            cok_pool_free_items(&game->pool);
        } else {
            vm->flags[0] = true; /* 1427: DS:b2f5 (=) */
            vm->flags[1] = false; /* DS:b2f6 (<>) */
        }
        bool money, items;
        if (!results(game, q->share, &money, &items)) return false; /* 1438 */
        if (money || items) { /* 1446-1455: the treasure menu (75cc:0e93) */
            if (!treasure_menu(game)) return false;
        }
        cok_pool_free(&game->pool); /* 1458: 63ee:0002 */
    }
    q->text_delay = true; /* 14eb: DS:ae65 */
    cok_ecl_store(vm, 0x01, 0);
    cok_ecl_store(vm, 0x2f, 0);
    cok_ecl_store(vm, 0x1c, 0);
    cok_ecl_store(vm, 0x1e, 0);
    cok_ecl_store(vm, 0x1d, 0);
    cok_ecl_store(vm, 0x1f, 0);
    cok_monster_clear(game); /* 1512: 75cc:151b */
    return true;           /* 1517: back to COMBAT's ending (monster.c) */
}

/* 75cc:135c's treasure branch with no monsters (5f07:081b's last): the weapons lost in combat
 * come back (1297; none here, a list that is not empty stops), var 0x2e is cleared (136a), the
 * flags = <> < > are cleared (2093:0015 with DS:b2f5 and 4: 136f), DS:c7df is cleared (1380),
 * then 0593 unless the demonstration runs (1385; then 1390 on, part 19d, cited). After 0593 the
 * mode is 6 (1390), 11b0 removes the enemies, and treasure_end (13ae-1517) finishes the branch,
 * then COMBAT's ending (monster.c) runs. */
void cok_dark_queen_no_monsters_treasure(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (game->lost_weapon_count != 0) {
        cok_adventure_not_yet(game, "the treasure", "dqk 75cc:1297 (a weapon lost in combat "
                                                    "goes back)");
        return;
    }
    cok_ecl_set_var(vm, COK_VAR_SURPRISE, 0); /* 136a */
    for (int k = 0; k < 4; ++k) vm->flags[k] = false; /* 136f */
    own(game)->monsters_present = false; /* 1380: DS:c7df */
    if (game->demo) {
        cok_adventure_not_yet(game, "the treasure", "dqk 75cc:1390 (the demonstration's: mode 6 "
                                                    "and 11b0, part 19d)");
        return;
    }
    if (!after_combat(game)) return;
    vm->mode = 6; /* 1390: DS:ae57 */
    enemies_removed(game);
    treasure_end(game);
}

void cok_dark_queen_set_monsters_present_for_test(cok_adventure *game, bool present)
{
    own(game)->monsters_present = present;
}

/* The small picture's frame (DS:b20e) and whether the menu that waits now animates it (the
 * picture is loaded and its menu_picture set by menu_wait_begin): the checks read them. */
unsigned cok_dark_queen_picture_frame(const cok_adventure *game)
{
    const queen *q = game->own;
    return q == NULL ? 0 : q->frame;
}

bool cok_dark_queen_picture_animating(const cok_adventure *game)
{
    const queen *q = game->own;
    return q != NULL && q->menu_picture;
}

/* 5045:0f6c's clock: 1396:044d(1, 1), a minute and its timers (pass_time). */
bool cok_dark_queen_round_clock(cok_adventure *game)
{
    return pass_time(game, 1, 1);
}

/* 5045:0f6c's "Your Teammate is Dying" (DS:5902), in the bar (177e:153c). */
void cok_dark_queen_teammate_dying(cok_adventure *game)
{
    queen_bar_message(game, "Your Teammate is Dying");
}

/* 5045:0f6c's Continue question (DS:4e98 "continue battle? ", 1bdb:0424): Yes is true, No and
 * an abort are not. */
bool cok_dark_queen_continue_battle(cok_adventure *game)
{
    return queen_yes_no(game, "continue battle? ") == 1;
}

/* 5045:0001's icons (204c:004d): the slots 0x0f-0x25 emptied, k = 0..0x16 at 00e0-00f9 (the
 * battle's 23 COMSPR slots, the cursor's 0x1e among them). The slots 0x98-0x9c are not emptied
 * here: icon_clear keeps no slot past ICON_SLOTS, and 204c:004d's index for them is not traced
 * (open: part 39). */
void cok_dark_queen_battle_icons(cok_adventure *game)
{
    for (unsigned k = 0; k <= 0x16; ++k) icon_clear(game, 0x0f + k);
}

/* 261b:05be after a turn (5045:066a): each colour cycle whose time has come steps once, at the
 * clock's ticks now (no random draw). */
void cok_dark_queen_turn_tail_cycles(cok_adventure *game)
{
    queen *q = own(game);
    if (q != NULL) cok_vga_cycle_colours(&q->palette, ticks(q));
}

cok_dark_queen_end_state cok_dark_queen_end_state_of(cok_adventure *game)
{
    cok_dark_queen_end_state s = {0};
    if (game->own == NULL) return s;
    s.standing = own(game)->standing;
    s.fled = own(game)->fled;
    s.monsters_present = own(game)->monsters_present;
    s.out = own(game)->out;
    s.b7a7 = own(game)->b7a7;
    s.share = own(game)->share;
    return s;
}

/* The temple (dqk 7825:0af6): "Heal View Repair Exit" by the bar's buttons, the party's list
 * drawn after each choice, Exit's too (5e22:023b). The mode is 1, the shop's frame off while the
 * screen is drawn, and the pool's coins cleared. Heal (heal) and View (86b8:0b0c, view_character)
 * run; Repair (7825:08fa) stops, cited: it ends the party's effects (148f:017d) and casts 849a's
 * spell. Exit leaves; no money is asked for. Escape is Exit; Up and Down pick a member
 * (member_key, 5e22:067c). Keys that run out end it. */
void cok_dark_queen_temple(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    static const char *const words[] = {"Heal", "View", "Repair", "Exit"};
    enum { TEMPLE_HEAL, TEMPLE_VIEW, TEMPLE_REPAIR, TEMPLE_EXIT };
    vm->mode = 1;
    game->shop_frame = false; /* 7825:0af6: DS:bee9 */
    cok_adventure_redraw(game);
    game->shop_frame = true;
    party_list(game);
    memset(game->pool.coins, 0, sizeof game->pool.coins);
    for (;;) {
        log_menu(game, words, 4);
        int choice = bar_menu(game, "", words, 4, true, false, 0, 0); /* 7825:0b43: anim 0 */
        if (choice < 0) return;
        int key = -1; /* a key that picks nothing */
        if (choice == 0x1b) {
            key = TEMPLE_EXIT;
        } else if (choice == 0x84 || choice == 0x88) {
            member_key(game, choice); /* 7825:0af6 calls 5e22:067c, not the special's scan */
        } else if (choice < 4) {
            key = choice;
        }
        if (key >= 0) log_choice(game, key);
        switch (key) {
        case TEMPLE_HEAL:
            if (!heal(game)) return;
            break;
        case TEMPLE_VIEW:
            /* The sheet leaves the screen as it was (the original's: the adventure's
             * screen by the mode, 177e:1ad1, as the adventure's View does after 2d99:0fef). */
            if (!view_character(game, NULL)) return;
            cok_adventure_redraw(game);
            break;
        case TEMPLE_REPAIR:
            cok_adventure_not_yet(game, "Repair in the temple", "dqk 7825:08fa (148f:017d)");
            return;
        case TEMPLE_EXIT:
            party_list(game); /* 7825:0bcd: after every choice, Exit's too */
            return;
        default: break;
        }
        if (vm->status != COK_ECL_OK || vm->abort) return;
        party_list(game);
    }
}
