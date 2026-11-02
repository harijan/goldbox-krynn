#define _POSIX_C_SOURCE 200809L /* opendir, strcasecmp */
#include "dark_queen.h"

#include "adventure.h"
#include "hlib.h"
#include "queen.h"
#include "queen_create.h"
#include "queen_items.h"
#include "queen_view.h"
#include "queen_train.h"
#include "queen_modify.h"
#include "vga.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The libraries the game draws from, read once and kept. */
enum {
    LIB_ALWAYS, LIB_FRAME, LIB_TITLE, LIB_BACK, LIB_WALLS_B, LIB_WALLS_C, LIB_PIC_A, LIB_PIC_B,
    LIB_PIC_C, LIB_BIGPIC, LIB_GEN, LIB_FINAL, LIB_DUNGCOM, LIB_CBODY, LIBS
};
static const char *const lib_names[LIBS] = {
    "ALWAYS.TLB", "FRAME.TLB", "TITLE.TLB", "BACK.TLB",   "8X8DB.TLB", "8X8DC.TLB",
    "PICA.TLB",   "PICB.TLB",  "PICC.TLB",  "BIGPIC.TLB", "GEN.TLB",   "FINAL.TLB",
    "DUNGCOM.TLB", "CBODY.TLB",
};
_Static_assert((int)LIB_DUNGCOM == (int)QUEEN_LIB_DUNGCOM && (int)LIB_CBODY == (int)QUEEN_LIB_CBODY,
               "queen.h numbers the libraries");

enum {
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

/* Let ms pass (1df9:052a), the cycles stepping as it does (05bb): in
 * steps to each cycle's time, so that a front end shows them. */
static void pass(cok_adventure *game, unsigned ms)
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
 * time with the text speed's delay (3 ms times 0x12) while slow; clear
 * first clears it and homes the cursor. A word that does not fit goes to
 * the next row (one that fits but for its space is printed first);
 * past the last row "Press [Return] to continue." (its bar left as it is)
 * and the window cleared. False if the keys ran out. */
static bool print_window(cok_adventure *game, const char *s, int x1, int y1, int x2, int y2,
                         unsigned fg, bool clear, bool slow)
{
    cok_text_cursor *c = &game->vm.cursor;
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
 * (1a62:0311, slow) and WHO's fill (1a62:09a1). */
static void window_text(cok_adventure *game, const char *text, unsigned fg, bool clear)
{
    print_window(game, text, 1, 0x11, 0x26, 0x16, fg, clear, true);
}

static void window_clear(cok_adventure *game)
{
    clear_cells(game, 1, 0x11, 0x26, 0x16);
}

/* The ECL's text window, cells 1-38 by 17-22, in colour fg. */
static bool print_colour(cok_adventure *game, const char *s, bool clear, unsigned fg)
{
    cok_adventure_log(game, "print", s);
    return print_window(game, s, 1, 0x11, 0x26, 0x16, fg, clear, true);
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
                          const rect *areas, const uint8_t *area_cursors, size_t area_count);

static int bar_menu(cok_adventure *game, const char *prompt, const char *const *items,
                    size_t count, bool dirs, bool anim, uint16_t timeout, int timed_out)
{
    return bar_menu_areas(game, prompt, items, count, dirs, anim, timeout, timed_out, NULL, NULL,
                          0);
}

/* bar_menu with areas of the screen after its buttons (1bdb:1429's: the
 * sheet's rows, which a click takes: 2aa6:15e3's hit, 1bdb:1206's), a
 * click on one giving AREA_MENU + its index; area_cursors, if not NULL,
 * the cursor shown over each (ALWAYS.TLB's record; NULL: the sword). */
static int bar_menu_areas(cok_adventure *game, const char *prompt, const char *const *items,
                          size_t count, bool dirs, bool anim, uint16_t timeout, int timed_out,
                          const rect *areas, const uint8_t *area_cursors, size_t area_count)
{
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
    int cols[ITEMS];
    rect rects[ITEMS];
    int col = 8 + 8 * (int)n;
    controls_clear(game);
    for (size_t i = 0; i < count; ++i) {
        int width = (int)strlen(items[i]);
        cols[i] = col;
        rects[i] = button_rect(BAR_ROW, col, width);
        control_add(game, rects[i]);
        button(game, BAR_ROW, col, items[i], width, 0);
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
            bool filtered = k == 0x1b || k == 0x60 ||
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
        button(game, BAR_ROW, cols[picked], items[picked], (int)strlen(items[picked]),
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

static void frame(cok_adventure *game, int screen)
{
    (void)screen;
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
 * The square's byte is the saved one (DS:c9f4), not recomputed here. */
static void view(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (game->party_killed) return;
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

/* PICTURE n (5f07:04a0): 0xff the view again where a picture was shown
 * (5e22:0000, view: on the overland its map again); from 0xf0 a big
 * picture (63ae:0321, 03b0), the party's place over it on the overland
 * (mode 3: 7065:0011), where the party's list is not drawn either (DS:b256);
 * below, a small picture, PIC<A, B or C>1 (63ae:0078: A below 0x80, B
 * below 0x97), its colours 0x20-0xff black first, record 1 drawn at cell
 * 3, 3 (63ae:000c), then its palette. */
static void picture(cok_adventure *game)
{
    queen *q = own(game);
    cok_ecl *vm = &game->vm;
    uint8_t n = (uint8_t)cok_ecl_value(vm, 0);
    if (n == 0xff) {
        if ((vm->last_mode != 4 || vm->mode == 4) && game->picture_shown) {
            game->picture_shown = false;
            view(game);
        }
        return;
    }
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
    unsigned which = n < 0x80 ? LIB_PIC_A : n < 0x97 ? LIB_PIC_B : LIB_PIC_C;
    enter_state(game, STATE_PICTURE);
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
    const char *items[COK_ECL_MAX_STRINGS];
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
    if (!print_window(game, vm->header_string, 1, 0x11, 0x26, 0x16, 7, true, true)) return;
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
        pass(game, 10);
    }
    return 0x20;
}

/* The routine PROGRAM n stops at (5f07:0baf's cases; the ones not ported). */
static const char *program_cite(unsigned n)
{
    switch (n) {
    case 0: return "dqk 7368:046f, then 5f07:0c15 (about 50-80 lines more)";
    case 3: return "dqk 78e4:17dd, a level drain";
    case 9: return "dqk 5f07:0b3b, Encamp";
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

/* PROGRAM n (5f07:0baf): Move mode (DS:af3d) is kept and off meanwhile, the
 * selection LOAD CHARACTER changed is restored first (DS:acc5), then n: 1 the
 * ending; 2 the quit (2093:002d, as the camp's Exit ends the game, without its
 * questions); 4-255 (0c8c) sets DS:b7f8, which 6c70:1685 (CALL 1) reads to
 * redraw the view and status, the port's view_changed; 0, 3 and 9 stop. Move
 * mode is put back after. */
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
    if (n == 1) {
        ending(game);
    } else if (n == 2) {
        cok_adventure_quit(game);
    } else if (n == 0 || n == 3 || n == 9) {
        char what[32];
        snprintf(what, sizeof what, "PROGRAM %u", n);
        cok_adventure_not_yet(game, what, program_cite(n));
    } else {
        vm->view_changed = true;
    }
    game->moving = moving;
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
        print_text(game, vm->string[0], clear);
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
        if (print_colour(game, line, true, 7)) { /* 6c70:32b2: colour 7 whatever 0x39 */
            cok_adventure_log(game, "menu", "~Press button or enter to continue");
            continue_prompt(game, true, game->picture_shown);
        }
        return true;
    }
    case COK_ECL_YES_NO: {
        /* 5f07:13c4: 1bdb:0424, the flags cleared first (2093:0015). */
        static const char *const items[] = {"Yes", "No"};
        memset(vm->flags, 0, sizeof vm->flags);
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
        unsigned fg = c == vm->character ? 0x0b
                      : c[0x18b] == 0 ? 0x00
                      : (cok_field_u8(L, c, COK_FIELD_HOSTILE) == 1 && vm->mode == 5) ? 0x02
                      : c[0xcd] != 0 ? 0x0d
                                     : 0x07;
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
                        unsigned fg, bool clear, bool slow)
{
    return print_window(game, s, x1, y1, x2, y2, fg, clear, slow);
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
 * a former paladin's, 177e:21bd, not ported), Exit always); Exit or
 * Escape leaves it; the others are not ported yet (the run stops).
 * False where the run stopped. */
static bool view_character(cok_adventure *game)
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
        if (!paladin && c[0xaf] > 0) {
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
            if (!queen_view_items(game, member)) return false;
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
        static const char *const routines[] = {"dqk 86b8:0f41", "dqk 7a87:10cb",
                                               "dqk 86b8:286a", "dqk 86b8:2c32",
                                               "dqk 86b8:2c32", "dqk 86b8:3200",
                                               "dqk 86b8:3319"};
        char what[32];
        snprintf(what, sizeof what, "View's %s", all[k]);
        cok_adventure_not_yet(game, what, routines[k]);
        return false;
    }
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
 * 0). The members' combat icons (604b:05d7, 204c:00df) are not loaded. */
static bool loaded(cok_adventure *game, uint8_t frame_was)
{
    cok_ecl *vm = &game->vm;
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

/* A message on the bar for the speed's time (1a62:0960: DS:1cd8's,
 * variable 0x12's): boxed from cell x + 1 in fg on bg, the bar again. */
static void message_at(cok_adventure *game, const char *s, int x, unsigned fg, unsigned bg)
{
    static const unsigned ms[] = {10, 100, 250, 500, 1000, 1500, 2000, 3000, 4000, 5000};
    bottom_bar(game);
    cok_adventure_log(game, "message", s);
    prompt_box(game, x, s, fg, bg);
    unsigned speed = cok_ecl_load_var(&game->vm, 0x12);
    pass(game, ms[speed < 10 ? speed : 9]);
    bottom_bar(game);
}

/* 177e:153c: s in the bar (white on grey, 1a62:0046 at row 24) for the
 * speed's time, in fg. */
static void bar_message_colour(cok_adventure *game, const char *s, unsigned fg)
{
    message_at(game, s, -1, fg, 8);
}

static void bar_message(cok_adventure *game, const char *s)
{
    bar_message_colour(game, s, 0x0f);
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
    cok_ecl *vm = &game->vm;
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
    uint8_t frame_was = (uint8_t)cok_ecl_load_var(vm, 0x3a);
    if (!find_file(dir, name, path, sizeof path) || !cok_adventure_restore(game, path) ||
        !loaded(game, frame_was)) {
        if (!find_file(dir, name, path, sizeof path))
            snprintf(game->error, sizeof game->error, "%.300s/%.60s: not found", dir, name);
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    bottom_bar(game);
    vm->mode = 0;
    return true;
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

/* The selected member removed (177e:1e31 with 0, 1): the selection the
 * one before it, or after it where it was the first, or the first; the
 * party's size (variable 0x20) one less. */
static void remove_selected(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    size_t i = cok_party_index(&game->party, vm->character);
    if (i >= game->party.count) return;
    cok_party_remove(&game->party, i);
    vm->character = cok_party_record(&game->party, i > 0 ? i - 1 : 0);
    cok_ecl_add_var(vm, COK_VAR_PARTY_SIZE, -1);
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
 * (604b:23a7) then removed (177e:1e31); an NPC (7368:0ee9: "Drop %s
 * forever? ", "Are you sure? ") not ported yet. False where the run
 * stopped. */
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
    remove_selected(game);
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

/* The camp's menu (7368:046f; with from_main, main's, with none, the
 * adventure's Encamp, not ported): the mode 0, the last mode the one it
 * was (DS:ae57, ae67); its screen (20b2:03ce; not again after a key that
 * is no button's), the party's list where a member is selected
 * (5e22:023b), its buttons (camp_flags). Then the full window (20b2:0030)
 * but for Remove, and: Create (66e5:2f95: queen_create.c), Modify
 * (6a87:1293: queen_modify.c), Train (6be8:0022) and Knight new Order
 * (7368:10c2: queen_train.c);
 * Remove; Load (a party not saved first asks "Game not saved. Load
 * anyway? ", DS:7e93), the party emptied, the game loaded, or a new
 * game's where none loads (64b2:04ec); Save; Begin Adventuring (with a
 * party) returns, the mode the last (4 for none), the adventure's screen
 * drawn as the mode has it, variable 0x30 0, the game not saved; Exit
 * From Game asks "Exit to DOS? " and, a party not saved, "Game not saved.
 * Exit anyway? " (No saving it: 604b:3395), then quits (2093:002d).
 * Returns the run's status, COK_ECL_OK with *begin set for Begin. */
static cok_ecl_status camp(cok_adventure *game, bool *begin)
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
            if (!view_character(game)) return vm->status;
            break;
        case CAMP_ADD:
            if (!add_character(game)) return vm->status;
            q->saved = false;
            break;
        case CAMP_REMOVE:
            if (!remove_character(game)) return vm->status;
            if (game->party.count == 0) {
                new_game(game); /* 2d99:1161: 64b2:04ec */
                vm->mode = 0;
            }
            q->saved = false; /* 7368:061b */
            break;
        case CAMP_LOAD:
            if (game->party.count > 0 && !q->saved) {
                int yes = yes_no(game, "Game not saved. Load anyway? ", true);
                if (yes < 0) return vm->status;
                if (!yes) break;
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
            vm->mode = vm->last_mode != 0 ? vm->last_mode : 4;
            /* 7368:07af-0804: a game not loaded (DS:b1ed) and small
             * picture 9 not shown (DS:ae42): the overland's frame in mode
             * 3 on the overland, else the adventure's screen, the party's
             * list after either; a game loaded: by the mode, 0 and 4 the
             * adventure's screen and the list, 3 the overland's frame, the
             * others nothing. */
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
 * 019f's camp case (mode 2, Encamp's vectors: PROGRAM 9, not ported) reads
 * the members' flags DS:a260 for [2..party size + 1], one past the party, and
 * sets a flag where an effect is reduced or ends; the other modes have every
 * flag at 1 (memset, 0x35 bytes). Here every member is counted in every mode,
 * as Champions' clock does; that is the difference Encamp will need. */
static bool moon(cok_adventure *game, uint16_t at, uint8_t days, unsigned base);

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
    while (minutes > 0) {
        uint16_t step = (uint16_t)(minutes < 11 ? minutes : 10);
        for (size_t i = 0; i < game->party.count; ++i)
            for (cok_effect *e = game->party.members[i]->effects; e != NULL; e = e->next) {
                if (e->duration == 0) continue;
                if (step < e->duration) {
                    e->duration = (uint16_t)(e->duration - step);
                    continue;
                }
                cok_adventure_not_yet(game, "an effect's end", "dqk 148f:017d");
                return false;
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
 * on (DS:a6b5), a click on the view's areas gives the same codes as those
 * keys (view_areas). Encamp ends it (4); Search turns variable 0x19's bit
 * 0 and draws the status; Look sets its bit 1, draws the status, passes
 * ten minutes and ends it (6); Area, Cast, View and Inv are not ported yet
 * (the run stops). Then the text window cleared where text was shown
 * (DS:b2fb). Returns the command, -1 where the run stopped. */
static int commands(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_store(vm, 3, 0);
    mouse_start(game);
    static const char *const items[] = {"Area", "Cast", "View", "Encamp", "Search", "Look",
                                        "Inv"};
    /* 1aef adds the view's areas only with the mouse on (1b3b). */
    size_t area_count = game->mouse.on ? sizeof view_areas / sizeof view_areas[0] : 0;
    int result = -1;
    for (bool done = false; !done;) {
        cok_adventure_log(game, "menu", "Area Cast View Encamp Search Look Inv");
        int choice = bar_menu_areas(game, "", items, 7, true, false, 0, 0, view_areas,
                                    view_cursors, area_count);
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
            if (!view_character(game)) return -1;
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

/* Begin Adventuring's start (5ebd:00ef after 7368:046f): the copy
 * protection (65cc:008b: a word from the journal, the game ending where
 * it is wrong; DARK-QUEEN.md's decision: logged and passed over, its
 * checks of DS:bec0-bec2 too), the adventure's screen (20b2:00e4), the
 * party's list and the status (or, on the overland, its frame), then the
 * game (5f07:10cd): the mode 4 (3 outside 3D, variable 0x22 0), the block
 * variable 0x16's or a new game's (4, its list drawn first), entered as
 * the ECL's NEWECL enters one (cok_adventure_enter); then its loop: a
 * command (6fea:0365); while it is Look (variable 0x19 above 1) or
 * Encamp, Look's: the view again and the location vector, searching or
 * not as before; Encamp's (6c70:34ab) not ported yet; then the
 * after-move vector; the last square in variables 0x17 and 0x18, the
 * step (6fea:0753), the view and the location vector, each NEWECL
 * entering its block (5f07:0ff4: cok_adventure_reenter). Outside 3D
 * (mode 3) its loop is not ported yet. */
static cok_ecl_status adventure(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_adventure_log(game, "protection", "the journal's word, passed over (dqk 65cc:008b)");
    if (vm->mode == 3 && cok_ecl_load_var(vm, 0x24) == 1) {
        overland_screen(game);
    } else {
        adventure_screen(game);
        party_list(game);
        status_line(game);
    }
    uint8_t block = (uint8_t)cok_ecl_load_var(vm, 0x16);
    /* A loaded game's first block keeps its locals (6c70:0001 with DS:b1ed
     * set, cok_adventure_restore's keep_vars); 10cd clears b1ed once it
     * has entered it. */
    cok_ecl_status status = cok_adventure_enter(game, block != 0 ? block : 4);
    own(game)->loaded_game = false;
    vm->keep_vars = false;
    while (status == COK_ECL_OK && !vm->abort) {
        if (vm->mode != 4) {
            cok_adventure_not_yet(game, "the adventure's commands outside 3D", "dqk 6fea:0605");
            return vm->status;
        }
        int key = commands(game);
        if (key < 0) return vm->status;
        while (status == COK_ECL_OK && !vm->abort &&
               (cok_ecl_load_var(vm, 0x19) > 1 || key == 4)) {
            if (key == 4) {
                cok_adventure_not_yet(game, "Encamp", "dqk 6c70:34ab");
                return vm->status;
            }
            uint16_t searching = cok_ecl_load_var(vm, 0x19) & 1;
            cok_ecl_store(vm, 0x19, 1);
            cok_adventure_view(game);
            status = cok_ecl_run(vm, vm->vectors[1]);
            if (status == COK_ECL_OK && !vm->abort && vm->reload) status = cok_adventure_reenter(game);
            cok_ecl_store(vm, 0x19, searching);
            if (status != COK_ECL_OK || vm->abort) break;
            key = commands(game);
            if (key < 0) return vm->status;
        }
        if (status != COK_ECL_OK || vm->abort) break;
        status = cok_ecl_run(vm, vm->vectors[0]);
        if (status != COK_ECL_OK || vm->abort) break;
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
    return status != COK_ECL_OK ? status : vm->status;
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
            cok_ecl_status status = camp(game, &begin);
            if (status != COK_ECL_OK || !begin || vm->abort) return status;
            return adventure(game);
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
    return bar_menu_areas(game, prompt, items, count, dirs, false, 0, 0, areas, NULL, area_count);
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
};
