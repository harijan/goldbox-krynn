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
    LIB_PIC_C, LIBS
};
static const char *const lib_names[LIBS] = {
    "ALWAYS.TLB", "FRAME.TLB", "TITLE.TLB", "BACK.TLB", "8X8DB.TLB", "8X8DC.TLB",
    "PICA.TLB",   "PICB.TLB",  "PICC.TLB",
};

enum {
    FONT_SIZE = 96 * 12,     /* GAME.FON: 0x20-0x7f, 12 rows each (dqk 25a7:04b5) */
    WINDOW = 0x17,           /* the window's grey, colour 8 through DS:9d86 */
    WALL_SETS = 3,
    WALL_COLOURS = 37,       /* each slot's colours from 0x20 + 37 * slot (8a8b:119d) */
    VIEW_AT = 24,            /* the view's box, cells 3-13 (8a8b:085a) */
    VIEW_END = 112,
    BAR_ROW = 188,           /* the bottom bar's buttons, 0x1f9e (1bdb:0801) */
};

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
     * b20d), the one shown. */
    hlib_library picture;
    bool picture_loaded;
    uint8_t first_frame, last_frame, frame;
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

/* A library's palette record id into the palette (2796:0536, 261b:00b1),
 * its colours from first moved by shift. */
static void set_palette(cok_adventure *game, const hlib_library *l, unsigned id, unsigned shift)
{
    const uint8_t *record;
    size_t size;
    if (hlib_record(l, id, &record, &size) != HLIB_OK || !cok_vga_is_palette(record, size)) return;
    if (shift == 0) {
        cok_vga_load_palette(&own(game)->palette, record, size);
        return;
    }
    uint8_t moved[8 + 3 * 256];
    if (size > sizeof moved) size = sizeof moved;
    memcpy(moved, record, size);
    unsigned first = (unsigned)(moved[2] | moved[3] << 8) + shift;
    moved[2] = (uint8_t)first;
    moved[3] = (uint8_t)(first >> 8);
    cok_vga_load_palette(&own(game)->palette, moved, size);
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

/* A framed button (1bdb:008e, raised): FRAME 13, 14 a character, 15 at
 * row, col; its text a row down, 0x87 (white with flags 0x40), and unless
 * flags hold 0x40 its first character again in white, the hotkey. */
static void button(cok_adventure *game, int row, int col, const char *s, unsigned flags)
{
    int width = (int)strlen(s);
    draw_frame_record(game, 13, row, col, NULL);
    for (int i = 0; i < width; ++i) draw_frame_record(game, 14, row, col + 8 * i, NULL);
    draw_frame_record(game, 15, row, col + 8 * (width - 1), NULL);
    text(game, row + 1, col, s, flags & 0x40 ? 0x8f : 0x87);
    if (!(flags & 0x41) && width > 0) {
        char first[2] = {s[0], '\0'};
        text(game, row + 1, col, first, 0x8f);
    }
}

/* 1bdb:036c: the bottom bar drawn again, FRAME 4 within rows 186-199. */
static void bottom_bar(cok_adventure *game)
{
    const cok_vga_clip clip = {186, 0, 200, 320};
    draw_frame_record(game, 4, 0, 0, &clip);
}

/* Whether key is Return (the '@' of 2aa6:000e: 0x0d or 0x0a). */
static bool is_return(int key)
{
    return key == '\r' || key == '\n';
}

/* 1bdb:0495: "Press [Return] to continue." and a wait for Return; other
 * keys beep (2102:0193) and are read past. False if the keys ran out. */
static bool continue_prompt(cok_adventure *game)
{
    bottom_bar(game);
    prompt_box(game, 7, "Press        to continue.", 0, 7);
    button(game, BAR_ROW, 112, "Return", 0x70);
    for (;;) {
        int key = cok_adventure_wait_key(game);
        if (key < 0) return false;
        if (is_return(key)) break;
        if (key == 0) cok_adventure_wait_key(game); /* an extended key's scan code */
    }
    bottom_bar(game);
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
 * past the last row "Press [Return] to continue." and the window cleared.
 * False if the keys ran out. */
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
                    if (slow) cok_adventure_wait(game, ms);
                }
            }
            c->x = x1;
            ++c->y;
            while (di <= last && s[di] == ' ') ++di;
            if (c->y > y2 && di < last) {
                *c = (cok_text_cursor){x1, y1};
                if (!continue_prompt(game)) return false;
                clear_cells(game, x1, y1, x2, y2);
            }
            continue;
        }
        for (; di <= si; ++di, ++c->x) {
            char one[2] = {s[di], '\0'};
            text(game, 8 * c->y, 8 * c->x, one, attribute);
            if (slow) cok_adventure_wait(game, ms);
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

/* The bottom bar's menu (1bdb:1e29, 08ef, 0801): a button an item from
 * x 8, each item's first letter its key (2aa6:000e: either case); Return
 * picks none, other keys beep. Escape gives 0 (6c70:0e3f's -1 stored as
 * 0) where escape. Returns the index, or -1 if the keys ran out. */
static int bar_menu(cok_adventure *game, const char *const *items, size_t count, bool escape)
{
    int col = 8;
    for (size_t i = 0; i < count; ++i) {
        button(game, BAR_ROW, col, items[i], 0x30);
        col += ((int)strlen(items[i]) + 1) * 8;
    }
    for (;;) {
        int key = cok_adventure_wait_key(game);
        if (key < 0) return -1;
        if (key == 0) {
            cok_adventure_wait_key(game); /* an extended key's scan code */
            continue;
        }
        if (key == 0x1b && escape) return 0;
        for (size_t i = 0; i < count; ++i) {
            int first = (unsigned char)items[i][0];
            if (first >= 'a' && first <= 'z') first -= 'a' - 'A';
            int upper = key >= 'a' && key <= 'z' ? key - ('a' - 'A') : key;
            if (first != 0 && upper == first) return (int)i;
        }
    }
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
static void frame_palette(cok_adventure *game)
{
    unsigned style = cok_ecl_load_var(&game->vm, 0x3a);
    unsigned id = style == 3 ? 43 : style == 4 ? 42 : style == 5 || style == 6 ? 44 : 41;
    const hlib_library *l = lib(game, LIB_FRAME);
    if (l != NULL) set_palette(game, l, id, 0);
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

static void frame(cok_adventure *game, int screen)
{
    (void)screen;
    adventure_screen(game);
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

static void view(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (game->party_killed) return;
    vm->square = cok_view_square(&game->view, vm->map_x, vm->map_y);
    draw_view(game);
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

/* PICTURE n (5f07:04a0): 0xff the view again where a picture was shown;
 * below 0xf0 a small picture, PIC<A, B or C>1 (63ae:0078: A below 0x80, B
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
        cok_adventure_not_yet(game, "big pictures", "dqk 63ae:0321");
        return;
    }
    unsigned which = n < 0x80 ? LIB_PIC_A : n < 0x97 ? LIB_PIC_B : LIB_PIC_C;
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
    for (unsigned i = 0x20; i < 0x100; ++i) cok_vga_set(&q->palette, i, 0, 0, 0);
    draw(game, &q->picture, 1, VIEW_AT, VIEW_AT, NULL, NULL);
    set_palette(game, &q->picture, 0, 0);
    game->picture_shown = true;
}

/* CALL 3 (5f07:0aba): the small picture's frame DS:b20e drawn at cell
 * 3, 3 (63ae:000c: record 1, then for a frame past 0 its record over it,
 * the overlays being smaller), its palette set; then the next frame, past
 * the last the loop's first; then the speed's delay, 100 ms times
 * variable 0x12 (1a62:0949). The frame's redraw it asks for where the
 * loop's first is not 0 (20b2:022c with 1: DS:58ca) is not drawn here. */
static void animate(cok_adventure *game)
{
    queen *q = own(game);
    if (!q->picture_loaded) return;
    draw(game, &q->picture, 1, VIEW_AT, VIEW_AT, NULL, NULL);
    if (q->frame != 0) draw(game, &q->picture, q->frame + 1u, VIEW_AT, VIEW_AT, NULL, NULL);
    set_palette(game, &q->picture, 0, 0);
    q->frame = q->frame >= q->last_frame ? q->first_frame : (uint8_t)(q->frame + 1);
    cok_adventure_wait(game, 100u * cok_ecl_load_var(&game->vm, 0x12));
}

static void horizontal(cok_adventure *game, bool ask)
{
    cok_ecl *vm = &game->vm;
    if (ask && !print_text(game, "what do you do?", false)) return; /* DS:7791 */
    const char *items[COK_ECL_MAX_STRINGS];
    size_t count = vm->operands < COK_ECL_MAX_STRINGS ? vm->operands : COK_ECL_MAX_STRINGS;
    for (size_t i = 0; i < count; ++i) items[i] = vm->string[i];
    log_menu(game, items, count);
    int choice = bar_menu(game, items, count, true);
    if (choice < 0) return;
    log_choice(game, choice);
    cok_ecl_store_as(vm, &vm->header[0], (uint16_t)choice);
    bottom_bar(game);
}

/* VERTICAL MENU (6c70:1e91): the prompt printed as PRINTCLEAR, the items a
 * row each below it (1bdb:1f25, 1b05:08b3), the one at the cursor in light
 * cyan, Up and Down moving it and Select (S, or Return) taking it; its
 * index stored as wide as the last item (a string: a word), then the
 * window cleared. */
static void vertical(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    size_t count = vm->operands < COK_ECL_MAX_STRINGS ? vm->operands : COK_ECL_MAX_STRINGS;
    cok_adventure_log(game, "list", vm->header_string);
    for (size_t i = 0; i < count; ++i) cok_adventure_log(game, "item", vm->string[i]);
    vm->cursor = (cok_text_cursor){1, 0x11};
    if (!print_window(game, vm->header_string, 1, 0x11, 0x26, 0x16, 7, true, true)) return;
    int top = vm->cursor.y + 1, rows = 0x16 - top;
    size_t at = 0;
    const char *select[1] = {"Select"}; /* DS:4eea */
    for (;;) {
        for (int i = 0; i < rows && (size_t)i < count; ++i)
            text(game, 8 * (top + i), 8, vm->string[i], (size_t)i == at ? 0x8b : 0x87);
        button(game, BAR_ROW, 8, select[0], 0x30);
        int key = cok_adventure_wait_key(game);
        if (key < 0) return;
        if (key == 0) {
            int scan = cok_adventure_wait_key(game);
            if (scan < 0) return;
            if (scan == 0x50 && at + 1 < count) ++at;
            if (scan == 0x48 && at > 0) --at;
            continue;
        }
        if (is_return(key) || key == 's' || key == 'S') break;
    }
    log_choice(game, (int)at);
    cok_ecl_operand o = vm->header[0];
    if (count > 0) o.type = vm->operand[count - 1].type;
    cok_ecl_store_as(vm, &o, (uint16_t)at);
    clear_cells(game, 1, 0x11, 0x26, 0x16);
    bottom_bar(game);
}

static bool opcode(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
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
        continue_prompt(game);
        return true;
    case COK_ECL_JOURNAL: {
        char line[64];
        snprintf(line, sizeof line, "This is recorded as journal entry %u.", /* DS:78a1 */
                 (unsigned)(uint8_t)cok_ecl_value(vm, 0));
        vm->cursor = (cok_text_cursor){1, 0x11};
        if (print_text(game, line, true)) {
            cok_adventure_log(game, "menu", "~Press button or enter to continue");
            continue_prompt(game);
        }
        return true;
    }
    case COK_ECL_YES_NO: {
        static const char *const items[] = {"Yes", "No"};
        memset(vm->flags, 0, sizeof vm->flags);
        log_menu(game, items, 2);
        int choice = bar_menu(game, items, 2, false);
        if (choice < 0) return true;
        log_choice(game, choice);
        vm->flags[choice == 0 ? 0 : 1] = true;
        bottom_bar(game);
        return true;
    }
    case COK_ECL_HORIZONTAL_MENU: horizontal(game, false); return true;
    case COK_ECL_ASK_MENU: horizontal(game, true); return true;
    case COK_ECL_VERTICAL_MENU: vertical(game); return true;
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
 * colours 0x10-0xff black, then its palette. */
static bool title_scene(cok_adventure *game, unsigned n)
{
    queen *q = own(game);
    hlib_library back, scene;
    if (!sub_library(game, LIB_TITLE, n <= 3 ? 1 : 5, &back) ||
        !sub_library(game, LIB_TITLE, n <= 3 ? n + 1 : n + 2, &scene))
        return false;
    draw(game, &back, 1, 0, 0, NULL, NULL);
    set_palette(game, &back, 0, 0);
    if (n == 6) set_palette(game, &scene, 0, 0);
    draw(game, &scene, 1, 0, 0, NULL, NULL);
    if (n == 3 || n == 4 || n == 5 || n == 7 || n == 8) draw(game, &scene, 2, 0, 0, NULL, NULL);
    for (unsigned i = 0x10; i < 0x100; ++i) cok_vga_set(&q->palette, i, 0, 0, 0);
    set_palette(game, &scene, 0, 0);
    return true;
}

/* 6523:000c's wait of 800 (7.91 seconds of the timer's 72.8 Hz): a key
 * read, or none (0) when the time ran out. -1 if the keys ran out. */
static int title_wait(cok_adventure *game)
{
    cok_adventure_flush(game);
    for (unsigned waited = 0; waited < 791; ++waited) {
        if (cok_adventure_key_pending(game)) return cok_adventure_wait_key(game);
        if (game->vm.abort) return -1;
        cok_adventure_wait(game, 10);
    }
    return 0;
}

/* The title (6523:0345): scenes 1-8, each until a key or its time: Space
 * the next, Escape to the menu, others end it; then ALWAYS's palette
 * again (6523:0319). Then main's menu (5ebd:00ef): the full window, the
 * name and version (5ebd:00b6, DS:4d73, 4d8b) and Play, Demo and Quit. */
static cok_ecl_status start(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    game->title = true;
    for (unsigned n = 1; n <= 8; ++n) {
        if (!title_scene(game, n)) {
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return vm->status;
        }
        int key = title_wait(game);
        if (key < 0) return vm->status;
        if (key == 0x1b || (key != 0 && key != ' ')) break;
    }
    game->title = false;
    set_palette(game, lib(game, LIB_ALWAYS), 0, 0);
    frame_palette(game);
    full_window(game);
    text(game, 40, 64, "The Dark Queen of Krynn", 0x87);
    text(game, 48, 32, "Version 1.1        July 29,1992", 0x87);
    static const char *const items[] = {"Play", "Demo", "Quit"};
    cok_adventure_log(game, "menu", "Play Demo Quit");
    int choice = bar_menu(game, items, 3, false);
    if (choice < 0) return COK_ECL_OK;
    log_choice(game, choice);
    if (choice == 2) {
        cok_adventure_quit(game);
        return COK_ECL_OK;
    }
    cok_adventure_not_yet(game, choice == 0 ? "start menu" : "demonstration",
                          "dqk 5ebd:00ef");
    return vm->status;
}

const cok_drawing cok_dark_queen_drawing = {queen_open, queen_close, frame,  view,
                                            opcode,     value,       stored, start};
