#include "dark_queen.h"

#include "adventure.h"
#include "hlib.h"
#include "vga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The libraries the game draws from, read once and kept. */
enum {
    LIB_ALWAYS, LIB_FRAME, LIB_TITLE, LIB_BACK, LIB_WALLS_B, LIB_WALLS_C, LIB_PIC_A, LIB_PIC_B,
    LIB_PIC_C, LIB_BIGPIC, LIB_GEN, LIB_FINAL, LIBS
};
static const char *const lib_names[LIBS] = {
    "ALWAYS.TLB", "FRAME.TLB", "TITLE.TLB", "BACK.TLB",   "8X8DB.TLB", "8X8DC.TLB",
    "PICA.TLB",   "PICB.TLB",  "PICC.TLB",  "BIGPIC.TLB", "GEN.TLB",   "FINAL.TLB",
};

enum {
    FONT_SIZE = 96 * 12,     /* GAME.FON: 0x20-0x7f, 12 rows each (dqk 25a7:04b5) */
    WINDOW = 0x17,           /* the window's grey, colour 8 through DS:9d86 */
    WALL_SETS = 3,
    WALL_COLOURS = 37,       /* each slot's colours from 0x20 + 37 * slot (8a8b:119d) */
    VIEW_AT = 24,            /* the view's box, cells 3-13 (8a8b:085a) */
    VIEW_END = 112,
    BAR_ROW = 188,           /* the bottom bar's buttons, 0x1f9e (1bdb:0801) */
    NO_BIG = 0xff,           /* DS:ae43 with no big picture loaded */
    CONTROLS = 16,           /* the buttons of a screen the mouse's cursor knows */
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
typedef struct {
    int top, left, bottom, right;
} rect;

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
    size_t control_count;
    bool cursor_shown; /* 22b7:2122 shows it, 10d7:0141 hides it */
    bool mouse_started;
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
typedef struct {
    int key;
    bool click;
    int x, y;
} input;

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

static void control_add(cok_adventure *game, rect r)
{
    queen *q = own(game);
    if (q->control_count < CONTROLS) q->controls[q->control_count++] = r;
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

/* The ECL's text window, cells 1-38 by 17-22, in colour 7 (3 where
 * variable 0x39 is set: 6c70:1cd3). */
static bool print_text(cok_adventure *game, const char *s, bool clear)
{
    unsigned fg = cok_ecl_load_var(&game->vm, 0x39) != 0 ? 3 : 7;
    cok_adventure_log(game, "print", s);
    return print_window(game, s, 1, 0x11, 0x26, 0x16, fg, clear, true);
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
static int bar_menu(cok_adventure *game, const char *prompt, const char *const *items,
                    size_t count, bool dirs, bool anim, uint16_t timeout, int timed_out)
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
    if (q->state == STATE_PICTURE || q->state == 2) q->picture_loaded = false;
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
 * c9f5, c9f6: never set from 0), then BACK's picture by the square: its
 * byte without 0x80 BACK 0x22 or, where the square's choice (8a8b:0807) is
 * not 0, the map's third; with 0x80 the map's first, second or third by
 * the choice, or none; ids from 0x20 one on at night (game byte 8, the
 * hour, below 6 or from 20). */
static void backdrop(cok_adventure *game, int x, int y)
{
    const cok_view *v = &game->view;
    fill(game, VIEW_AT, VIEW_AT, 68, VIEW_END, 0);
    fill(game, 68, VIEW_AT, 70, VIEW_END, 0);
    fill(game, 70, VIEW_AT, VIEW_END, VIEW_END, 0);
    uint8_t choice = cok_view_backdrop_choice(v, x, y);
    unsigned id;
    if (!(cok_view_square(v, x, y) & 0x80))
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
         * found yet (8a8b:085a loads them only with slot 0's), nor checked
         * against it: no check shows the view after a picture. */
        for (unsigned slot = 0; slot < WALL_SETS; ++slot)
            if (q->wall_loaded[slot]) set_palette(game, &q->walls[slot], 0, WALL_COLOURS * slot);
    }
    backdrop(game, vm->map_x, vm->map_y);
    view_walls(game, vm->map_x, vm->map_y, vm->direction & 6);
}

static bool big_picture(cok_adventure *game, unsigned n);

/* The view again (5e22:0000): on the overland (variables 0x22 0 and 0x24
 * 1) its map, variable 0x3e's big picture, whole (without the party's
 * place: the callers that want it draw it); else the 3D view in its box,
 * the rest of the screen as it is. */
static void view(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (game->party_killed) return;
    vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
    if (cok_ecl_load_var(vm, 0x22) == 0 && cok_ecl_load_var(vm, 0x24) == 1) {
        if (!big_picture(game, cok_ecl_load_var(vm, 0x3e)))
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
    } else {
        draw_view(game);
    }
    game->redraw = false;
}


/* Opcodes. */

/* LOAD FILES map, wall set (6c70:1dec): the map from GEO.GLB
 * (8a8b:12e3: variables 0x13 and 0xc6 its id, slots 0 and 1 freed), 0x03
 * cleared, the wall set's id kept (DS:ca03). */
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
    game->files_loaded = game->map_loaded = true;
}

/* The party's place on the overland map (7065:0011): ALWAYS 29 at the
 * cell past variables 0x25 and 0x26, the map's square. */
static void marker(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    int col = 8 * (cok_ecl_load_var(vm, 0x25) + 1), row = 8 * (cok_ecl_load_var(vm, 0x26) + 1);
    draw(game, lib(game, LIB_ALWAYS), 0x1d, row, col, NULL, NULL);
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
        if (vm->mode == 3) marker(game);
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
    if (ask && !print_text(game, "what do you do?", false)) return; /* DS:7791 */
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

/* VERTICAL MENU (6c70:1e91, 0f4a, 1bdb:1f25, the list 1b05:08b3): the
 * prompt printed as PRINTCLEAR, the items a row each from the row below
 * it to row 21 (1b05:0055: each row cleared, the current one in light
 * cyan, 0x8b, the others 0x87), "Select" on the bar (its 'S', and Return
 * as its second hotkey). Up or Home the one before, Down or End the one
 * after, wrapping at the ends; PgUp the top row shown, or a page up from
 * it; PgDn the bottom one, or a page down; the rows scrolling to show the
 * current one (moving up at the top, down at the bottom). Select, or
 * Escape (or '`'), ends it, the current one's index stored either way, as
 * wide as the last item (a string: a word); then the window cleared. A
 * click on a row makes it current, on the current one selects it; a
 * click on Select selects. With more items than rows the original draws
 * a scroll bar too (21e9:082d, not decoded): not drawn here. */
static void vertical(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    size_t count = vm->operands < COK_ECL_MAX_STRINGS ? vm->operands : COK_ECL_MAX_STRINGS;
    cok_adventure_log(game, "list", vm->header_string);
    for (size_t i = 0; i < count; ++i) cok_adventure_log(game, "item", vm->string[i]);
    vm->cursor = (cok_text_cursor){1, 0x11};
    if (!print_window(game, vm->header_string, 1, 0x11, 0x26, 0x16, 7, true, true)) return;
    int y1 = vm->cursor.y + 1, rows = 0x16 - y1, cols = 0x26 - 1;
    if (rows < 1) rows = 1;
    if ((size_t)rows < count) --cols; /* room for the scroll bar */
    size_t at = 0, top = 0;
    static const char *const select[] = {"Select"}; /* DS:4eea */
    bottom_bar(game);
    button(game, BAR_ROW, 8, select[0], 6, 0);
    controls_clear(game);
    rect sel = button_rect(BAR_ROW, 8, 6);
    rect list = {8 * y1, 8, 8 * (y1 + rows), 8 * (1 + cols)};
    control_add(game, sel);
    control_add(game, list);
    bool done = false;
    while (!done) {
        if (at < top) top = at;
        if (at >= top + (size_t)rows) top = at + 1 - (size_t)rows;
        for (int i = 0; i < rows; ++i) {
            clear_cells(game, 1, y1 + i, cols, y1 + i);
            size_t item = top + (size_t)i;
            if (item < count)
                text(game, 8 * (y1 + i), 8, vm->string[item], item == at ? 0x8b : 0x87);
        }
        input in;
        int key = read_input(game, &in);
        if (key < 0 && key != COK_KEY_TIMEOUT) {
            controls_clear(game);
            return;
        }
        if (in.click) {
            if (inside(&sel, in.x, in.y)) {
                done = true;
            } else if (inside(&list, in.x, in.y) && count > 0) {
                size_t item = top + (size_t)((in.y - 8 * y1) / 8);
                if (item >= count) item = count - 1;
                if (item == at) done = true;
                at = item;
            } else {
                beep(game);
            }
            continue;
        }
        size_t page = rows > 1 ? (size_t)rows - 1 : 1, bottom = top + (size_t)rows - 1;
        if (bottom >= count) bottom = count > 0 ? count - 1 : 0;
        switch (in.key) {
        case KEY_UP: case KEY_HOME: at = at > 0 ? at - 1 : count - 1; break;
        case KEY_DOWN: case KEY_END: at = at + 1 < count ? at + 1 : 0; break;
        case KEY_PGUP: at = at != top ? top : at > page ? at - page : 0; break;
        case KEY_PGDN:
            at = at != bottom ? bottom : at + page < count ? at + page : count - 1;
            break;
        case '\r': case '\n': case 's': case 'S': case 0x1b: case 0x60: done = true; break;
        case KEY_ALT + 'S': case KEY_ALT + 'T': break; /* the sound's toggles */
        default: beep(game); break;
        }
        if (count == 0) at = 0;
    }
    controls_clear(game);
    button(game, BAR_ROW, 8, select[0], 6, BUTTON_PRESSED);
    log_choice(game, (int)at);
    cok_ecl_operand o = vm->header[0];
    if (count > 0) o.type = vm->operand[count - 1].type;
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
    char blank[39];
    memset(blank, ' ', 38);
    blank[38] = '\0';
    size_t n = 0;
    line[0] = '\0';
    prompt_box(game, 0, blank, 7, 8);
    for (;;) {
        char shown[48];
        snprintf(shown, sizeof shown, "%s ", line);
        text(game, 189, 16, shown, 0x87);
        fill(game, 189, 16 + 8 * (int)n, 197, 24 + 8 * (int)n, colour(7));
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
            text(game, 189, 16, shown, 0x87);
            snprintf(shown, sizeof shown, "%s ", line);
            text(game, 189, 16, shown, 0x87);
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
        if (print_text(game, line, true)) {
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
    case COK_ECL_PROGRAM: {
        /* 5f07:0baf, its cases at 5f07:0c01: 1 the ending; the others
         * need the party (the camp, the vault, ...). */
        unsigned n = (uint8_t)cok_ecl_value(vm, 0);
        if (n == 1) {
            ending(game);
        } else {
            char what[32];
            snprintf(what, sizeof what, "PROGRAM %u", n);
            cok_adventure_not_yet(game, what, "dqk 5f07:0baf");
        }
        return true;
    }
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
    unsigned id = 2;
    for (size_t i = 0; i < q->control_count; ++i)
        if (inside(&q->controls[i], x, y)) id = 1;
    const uint8_t *record;
    size_t size;
    cok_vga_image image = {0};
    if (hlib_record(always, id, &record, &size) != HLIB_OK ||
        cok_vga_decode(record, size, always, &image) != COK_VGA_OK)
        return;
    /* At the pointer less the hotspot, to the pixel (not to a plane's
     * four, as the game's pictures). */
    cok_picture copy = game->screen;
    copy.pixels = pixels;
    cok_vga_draw(&copy, &image, x - image.x, y - image.y, NULL, NULL);
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

/* The camp's buttons (7368:01a7) in the order made, which is the index
 * returned: their words (DS:7dce-7e84), hotkeys, row and column; and
 * whether each is on without a party (DS:3c3e-3c49). */
static const struct {
    const char *text;
    char key;
    int row, col;
    bool alone;
} camp_items[] = {
    {"Create Character", 'C', 118, 168, true},   {"Delete Character", 'D', 132, 168, true},
    {"Modify Character", 'M', 146, 8, false},    {"Train Character", 'T', 160, 8, false},
    {"Knight new Order", 'K', 174, 8, false},    {"View Character", 'V', 188, 8, false},
    {"Add Character", 'A', 118, 8, true},        {"Remove Character", 'R', 132, 8, false},
    {"Load Saved Game", 'L', 146, 168, true},    {"Save Current Game", 'S', 160, 168, false},
    {"Begin Adventuring", 'B', 174, 168, false}, {"Exit From Game", 'E', 188, 168, true},
};
enum { CAMP_ITEMS = sizeof camp_items / sizeof *camp_items, CAMP_WIDTH = 18 };

/* The camp's menu from main (7368:046f with 1), with no party: its
 * screen, its twelve buttons of 18 characters, those a party needs
 * disabled (sunken, no hotkey), read by their letters or a click. Exit
 * From Game asks "Exit to DOS? " (DS:3cd2) and quits (2093:002d); Load
 * Saved Game asks "load from where? " (604b:2e87), Exit going back; the
 * rest, and loading, are not ported yet: the run stops. Returns the run's
 * status. */
static cok_ecl_status camp(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    enter_state(game, STATE_CAMP);
    for (;;) {
        if (!camp_screen(game)) {
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return vm->status;
        }
        controls_clear(game);
        rect rects[CAMP_ITEMS];
        char line[512] = "";
        for (size_t i = 0; i < CAMP_ITEMS; ++i) {
            bool on = camp_items[i].alone;
            button(game, camp_items[i].row, camp_items[i].col, camp_items[i].text, CAMP_WIDTH,
                   on ? 0 : BUTTON_DISABLED);
            rects[i] = button_rect(camp_items[i].row, camp_items[i].col, CAMP_WIDTH);
            control_add(game, rects[i]);
            if (on) {
                size_t n = strlen(line);
                snprintf(line + n, sizeof line - n, "%s%s", n > 0 ? " " : "",
                         camp_items[i].text);
            }
        }
        cok_adventure_log(game, "menu", line);
        int picked = -1;
        while (picked < 0) {
            input in;
            int key = read_input(game, &in);
            if (key < 0 && key != COK_KEY_TIMEOUT) {
                controls_clear(game);
                return vm->status;
            }
            for (size_t i = 0; i < CAMP_ITEMS && picked < 0; ++i) {
                if (!camp_items[i].alone) continue;
                if (in.click ? inside(&rects[i], in.x, in.y) : hotkey(camp_items[i].key, in.key))
                    picked = (int)i;
            }
            /* 7368:000d takes Alt-S and Alt-T; the rest beep. */
            if (picked < 0 && in.key != KEY_ALT + 'S' && in.key != KEY_ALT + 'T') beep(game);
        }
        controls_clear(game);
        button(game, camp_items[picked].row, camp_items[picked].col, camp_items[picked].text,
               CAMP_WIDTH, BUTTON_PRESSED);
        log_choice(game, picked);
        full_window(game); /* 7368:05a4: but for Remove */
        switch (picked) {
        case 11: { /* 7368:081a */
            int yes = yes_no(game, "Exit to DOS? ", false);
            if (yes < 0) return vm->status;
            if (yes) {
                cok_adventure_quit(game);
                return COK_ECL_OK;
            }
            break;
        }
        case 8: { /* 7368:06e4, 604b:2e87 */
            static const char *const where[] = {"Tdqk", "Dkk", "Exit"}; /* DS:500c */
            cok_adventure_log(game, "menu", "Tdqk Dkk Exit");
            int choice;
            do {
                choice = bar_menu(game, "load from where? ", where, 3, false, false, 0, 0);
                if (choice < 0) return vm->status;
            } while (choice > 2 && choice != 0x1b);
            log_choice(game, choice);
            if (choice == 0 || choice == 1) {
                cok_adventure_not_yet(game, "saved games", "dqk 604b:2e87");
                return vm->status;
            }
            bottom_bar(game);
            break;
        }
        case 0: cok_adventure_not_yet(game, "Create Character", "dqk 66e5:2f95"); return vm->status;
        case 1: cok_adventure_not_yet(game, "Delete Character", "dqk 7368:0c23"); return vm->status;
        default: cok_adventure_not_yet(game, "Add Character", "dqk 7368:08dc"); return vm->status;
        }
    }
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
        if (choice != 1) return camp(game);
        bool stopped;
        cok_ecl_status status = demonstration(game, &stopped);
        if (status != COK_ECL_OK || vm->abort) return status;
        if (!stopped && !title(game)) return vm->status;
        frame_palette_of(game, 0);
        choice = main_menu(game, 0x4b0);
    }
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
};
