#include "arena.h"
#include "round.h"

#include "monster.h"
#include "screen.h"
#include "treasure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    char log[16384];
    unsigned pauses[512], pause_count; /* The delay hook's lengths. */
    int keys;                          /* Keys left to read (Enter). */
    int sample_x, sample_y;            /* A screen pixel kept at each pause, */
    uint8_t samples[512];              /* if sample_x is set. */
    uint8_t unders[4][288];            /* DS:4b7c at the last four pauses. */
    unsigned forget;                   /* At pause forget - 1, as if no */
} script;                              /* save had written DS:4b7c. */

static script s;
static cok_adventure game;

static int key(void *context)
{
    script *t = context;
    if (t->keys == 0) return -1;
    --t->keys;
    return '\r';
}

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g;
    script *t = context;
    size_t used = strlen(t->log);
    snprintf(t->log + used, sizeof t->log - used, "%s: %s;", kind, text);
}

static uint8_t pixel(const cok_picture *p, size_t frame, int x, int y);

static void delay(cok_adventure *g, unsigned ms, void *context)
{
    script *t = context;
    if (t->pause_count < 512) {
        t->pauses[t->pause_count] = ms;
        if (t->sample_x != 0)
            t->samples[t->pause_count] = pixel(&g->screen, 0, t->sample_x, t->sample_y);
    }
    if (g->combat.under.pixels != NULL)
        memcpy(t->unders[t->pause_count % 4], g->combat.under.pixels, 288);
    ++t->pause_count;
    if (t->pause_count == t->forget) memset(g->combat.under_set, 0, sizeof g->combat.under_set);
}

/* The pixel at x, y of frame of p. */
static uint8_t pixel(const cok_picture *p, size_t frame, int x, int y)
{
    uint8_t b = p->pixels[frame * p->frame_size + (size_t)y * p->units * 4 + (size_t)x / 2];
    return x % 2 == 0 ? b >> 4 : b & 15;
}

static bool masked(const cok_picture *p, int x, int y)
{
    uint8_t b = p->mask[(size_t)y * p->units * 4 + (size_t)x / 2];
    return (x % 2 == 0 ? b >> 4 : b & 15) != 0;
}

/* Whether cell (8 by 8) x, y of a and b hold the same pixels. */
static bool same_cell(const cok_picture *a, int ax, int ay, const cok_picture *b, int bx, int by)
{
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (pixel(a, 0, ax * 8 + x, ay * 8 + y) != pixel(b, 0, bx * 8 + x, by * 8 + y))
                return false;
    return true;
}

/* Whether map cell (24 by 24) at screen position xs, ys shows tile frame
 * of the combat tile set. */
static bool shows_tile(int xs, int ys, unsigned frame)
{
    const cok_picture *t = &game.combat.tiles;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            if (pixel(&game.screen, 0, 8 + xs * 24 + x, 8 + ys * 24 + y) != pixel(t, frame, x, y))
                return false;
    return true;
}

/* The first colour other than black in screen cell x, y. */
static uint8_t ink(int x, int y)
{
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) {
            uint8_t v = pixel(&game.screen, 0, x * 8 + c, y * 8 + r);
            if (v != 0) return v;
        }
    return 0;
}

static bool blank(int x1, int y1, int x2, int y2)
{
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x)
            if (ink(x, y) != 0) return false;
    return true;
}

/* Whether text shows at cell x, y in fg, as the font draws it. */
static bool shows_text(const char *text, int x, int y, uint8_t fg)
{
    static cok_picture scratch;
    if (scratch.pixels == NULL) CHECK(cok_picture_create(&scratch, 40, 8, 1, 0) == COK_PICTURE_OK);
    cok_picture_fill(&scratch, 0, 0, 40, 8, 0);
    cok_text_string(&scratch, &game.font, text, 0, 0, fg, 0);
    for (size_t i = 0; i < strlen(text); ++i)
        if (!same_cell(&game.screen, x + (int)i, y, &scratch, (int)i, 0)) return false;
    return true;
}

static void reset(void)
{
    cok_party_free(&game.party);
    game.vm.character = game.vm.saved_character = NULL;
    memset(game.vm.mem4b00, 0, sizeof game.vm.mem4b00);
    memset(game.vm.mem7c00, 0, sizeof game.vm.mem7c00);
    memset(game.view.map, 0, sizeof game.view.map);
    game.view.wrap = false;
    game.vm.mode = 4;
    game.vm.mem4b00[0xe6] = 1;
    game.vm.file = 1;
    game.vm.map_x = game.vm.map_y = 7;
    game.vm.direction = 0;
    game.vm.seed = 1;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.speed = 4;
    game.combat.active = false;
    game.combat.show_actions = game.combat.panel = false;
    memset(&s, 0, sizeof s);
}

static const cok_combatant *entry(unsigned n)
{
    return &game.combat.combatant[n];
}

static cok_character *record(char name, uint8_t side, uint8_t slot)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = 1;
    r[1] = (uint8_t)name;
    r[0x10] = r[0x11] = r[0x16] = r[0x17] = 12;
    r[0x62] = r[0x197] = 10;
    r[0x113] = 50;
    r[0xcf] = 1;
    r[0x189] = 1;
    r[0x18a] = side;
    r[0x137] = slot;
    CHECK(cok_party_append(&game.party, c));
    return c;
}

/* A battle on a 3D map with no walls: the party of two, A and B, at 7, 7
 * facing north, and two monsters, C and D, before them; A and B
 * have the icons of CPIC1 records 1 and 4, the monsters 5. */
static void battle(void)
{
    reset();
    record('A', 0, 8);
    record('B', 0, 9);
    record('C', 1, 10);
    record('D', 1, 10);
    game.vm.mem7c00[0x33e] = 2;
    game.vm.mem7c00[0x2c1] = 0;
    game.vm.character = cok_party_record(&game.party, 0);
    CHECK(cok_arena_load_icon(&game, "CPIC", 1, 8));
    CHECK(cok_arena_load_icon(&game, "CPIC", 4, 9));
    CHECK(cok_arena_load_icon(&game, "CPIC", 5, 10));
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
    CHECK(cok_combat_setup(&game));
    s.pause_count = 0;
    s.log[0] = '\0';
    if (getenv("ARENA_SHOW") != NULL)
        for (unsigned n = 1; n < game.combat.count; ++n)
            fprintf(stderr, "%u at %d,%d screen %d,%d size %u\n", n, entry(n)->x, entry(n)->y,
                    entry(n)->screen_x, entry(n)->screen_y, entry(n)->size);
}

/* The tables match the original's data segment (DS 0x1bc6) in
 * build/START_FULL.EXE, if it has been built. */
static void test_tables(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("arena: build/START_FULL.EXE not built; tables not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    for (size_t i = 0; i < cok_arena_table_count; ++i) {
        const cok_ds_table *t = &cok_arena_tables[i];
        for (size_t k = 0; k < t->size; ++k) CHECK(exe[ds + t->offset + k] == t->bytes[k]);
    }
    /* The frame's three side columns (DS:0ebd, 0ed4, 0eeb) are the same. */
    for (unsigned k = 0; k < 23; ++k)
        CHECK(exe[ds + 0xebd + k] == exe[ds + 0xed4 + k] &&
              exe[ds + 0xed4 + k] == exe[ds + 0xeeb + k]);
    /* The letters of the icon sizes (DS:0875): none, S and T. */
    CHECK(exe[ds + 0x875] == 0 && exe[ds + 0x876] == 'S' && exe[ds + 0x877] == 'T');
}

/* 1128:04c1: the whole screen but row 24 cleared, the moons' row, three
 * columns down to row 22 and row 22 across, drawn over their ends. */
static void test_frame(void)
{
    cok_picture *screen = &game.screen;
    cok_picture_fill(screen, 0, 0, 40, 200, 7);
    uint16_t moons[3] = {0, 1, 2};
    cok_screen_combat(screen, &game.view.tiles[4], moons);
    static cok_picture expected;
    CHECK(cok_picture_create(&expected, 40, 200, 1, 0) == COK_PICTURE_OK);
    const cok_picture *t = &game.view.tiles[4];
    cok_picture_draw(&expected, t, 0x14 + 4, 22, 5, 0, NULL);
    CHECK(same_cell(screen, 22, 5, &expected, 22, 5) && same_cell(screen, 0, 12, &expected, 22, 5));
    CHECK(same_cell(screen, 39, 20, &expected, 22, 5));
    cok_picture_draw(&expected, t, 0x14 + 3, 22, 1, 0, NULL);
    CHECK(same_cell(screen, 22, 1, &expected, 22, 1));
    cok_picture_draw(&expected, t, 0x14 + 5, 22, 21, 0, NULL);
    CHECK(same_cell(screen, 39, 21, &expected, 22, 21));
    /* Row 22 across, over the columns' ends (tile 5). */
    cok_picture_draw(&expected, t, 0x14, 22, 22, 0, NULL);
    CHECK(same_cell(screen, 22, 22, &expected, 22, 22));
    CHECK(same_cell(screen, 0, 22, &expected, 22, 22) && same_cell(screen, 10, 22, &expected, 22, 22) &&
          same_cell(screen, 30, 22, &expected, 22, 22));
    /* The moons by phase, at columns 8, 19 and 30. */
    cok_picture_draw(&expected, t, 0x14 + 10, 8, 0, 0, NULL);
    cok_picture_draw(&expected, t, 0x14 + 7, 19, 0, 0, NULL);
    CHECK(same_cell(screen, 8, 0, &expected, 8, 0) && same_cell(screen, 19, 0, &expected, 19, 0));
    /* Inside, and row 23, cleared; row 24 kept. */
    CHECK(blank(1, 1, 21, 21) && blank(23, 1, 38, 21) && blank(0, 23, 39, 23));
    CHECK(pixel(screen, 0, 0, 24 * 8) == 7);
    cok_picture_free(&expected);
}

/* The map in the view's box: each cell's tile, then the combatants, the
 * party's facing north-west (7) mirrored, the monsters' south-east (3) as
 * drawn. */
static void test_map(void)
{
    battle();
    const cok_combat *c = &game.combat;
    CHECK(c->view_x == 24 && c->view_y == 10);
    CHECK(entry(1)->x == 27 && entry(1)->y == 13 && entry(1)->screen_x == 3);
    for (int ys = 0; ys < 7; ++ys)
        for (int xs = 0; xs < 7; ++xs) {
            uint8_t occupant = c->occupant[c->view_y + ys][c->view_x + xs];
            if (occupant != 0) continue;
            uint8_t value = c->cells[c->view_y + ys][c->view_x + xs];
            CHECK(shows_tile(xs, ys, cok_combat_terrain[value].tile));
        }
    /* A's icon, CPIC1 1, mirrored: its pixel at 23 - x, over the floor
     * where its mask keeps it. */
    const cok_picture *icon = &game.icons[8][0];
    int ox = 8 + entry(1)->screen_x * 24, oy = 8 + entry(1)->screen_y * 24;
    unsigned drawn = 0;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) {
            uint8_t want = masked(icon, x, y) ? pixel(&c->tiles, 0x16, 23 - x, y)
                                              : pixel(icon, 0, x, y);
            if (!masked(icon, x, y)) ++drawn;
            CHECK(pixel(&game.screen, 0, ox + 23 - x, oy + y) == want);
        }
    CHECK(drawn > 50);
    /* C faces 3: drawn as it is. */
    const cok_combatant *m = entry(3);
    icon = &game.icons[10][0];
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            if (!masked(icon, x, y))
                CHECK(pixel(&game.screen, 0, 8 + m->screen_x * 24 + x, 8 + m->screen_y * 24 + y) ==
                      pixel(icon, 0, x, y));
    /* Facing 4, the first mirrored. */
    icon = &game.icons[8][0];
    CHECK(cok_arena_icon(&game, 5, 5, 4, 0, 8));
    cok_arena_show(&game);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            if (!masked(icon, x, y))
                CHECK(pixel(&game.screen, 0, 8 + 5 * 24 + 23 - x, 8 + 5 * 24 + y) == pixel(icon, 0, x, y));
    /* An icon drawn with none of its rows or bytes in the map's buffer
     * (127f:27af counts them as 65536); further off, nothing. */
    CHECK(!cok_arena_icon(&game, -1, 2, 0, 0, 8) && strstr(game.error, "65536") != NULL);
    CHECK(!cok_arena_icon(&game, 2, -1, 0, 0, 8) && !cok_arena_icon(&game, 7, 2, 0, 0, 8));
    CHECK(!cok_arena_icon(&game, 2, 7, 0, 0, 8));
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    CHECK(cok_arena_icon(&game, -2, 2, 0, 0, 8) && cok_arena_icon(&game, 2, 8, 0, 0, 8));
    /* The frame around it, "A battle begins..." still on row 24. */
    CHECK(shows_text("A battle begins...", 0, 24, 10));
    CHECK(strstr(s.log, "unported") == NULL);
}

/* 6beb:07a9 reads cells past a side of the map from the row before or
 * after, before the first row the map's own bytes, and above them the heap
 * around it, where the port stops. */
static void test_off_the_map(void)
{
    battle();
    cok_combat *c = &game.combat;
    /* Column 50 of row 12 is column 0 of row 13. */
    c->cells[13][0] = 0x1a; /* a table */
    c->cells[12][49] = COK_COMBAT_FLOOR;
    CHECK(cok_arena_centre(&game, 46, 12, 0xff, 8));
    CHECK(c->view_x == 43 && c->view_y == 9);
    CHECK(shows_tile(6, 3, 0x16) && shows_tile(6, 4, 0x16));
    c->view_x = 47;
    c->view_y = 9;
    CHECK(cok_arena_centre(&game, 50, 12, 0xff, 8));
    CHECK(shows_tile(3, 3, 0x22) && shows_tile(2, 3, 0x16));
    /* At row 0, cells -3 to -1 are the map's bytes +4 to +6: the cursor
     * (0), its footprint (1) and the sight flag. */
    c->view_x = -3;
    c->view_y = 0;
    c->see_all = 0x1b; /* a chair, to tell them apart */
    CHECK(cok_arena_centre(&game, 0, 3, 0xff, 8));
    CHECK(shows_tile(0, 0, 0) && shows_tile(1, 0, 0) && shows_tile(2, 0, 0x23));
    c->see_all = 0;
    /* A row above the map is the heap's. */
    c->view_x = 10;
    c->view_y = -1;
    CHECK(!cok_arena_centre(&game, 13, 2, 0xff, 8));
    CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.error, "cell 10,-1") != NULL);
    /* A tile no battle loads (0x21, terrain 0x41). */
    battle();
    game.combat.cells[13][24] = 0x41;
    CHECK(!cok_arena_redraw(&game) && strstr(game.error, "tile 0x21") != NULL);
}

/* 6beb:096b: a scroll redraws every combatant that can act or whose
 * status is 10, not the fallen; within the margin only the cell left and
 * the cell reached are drawn. */
static void test_centre(void)
{
    battle();
    cok_combat *c = &game.combat;
    cok_character *b = game.party.members[1], *cm = game.party.members[2];
    const cok_combatant *ea = entry(1), *eb = entry(2), *ec = entry(3);
    b->record[0x189] = 0;
    b->record[0x188] = 6;
    cm->record[0x189] = 0;
    cm->record[0x188] = 10;
    CHECK(cok_arena_centre(&game, 27, 13, 0xff, 8));
    CHECK(shows_tile(eb->screen_x, eb->screen_y, 0x16));
    CHECK(!shows_tile(ec->screen_x, ec->screen_y, 0x16));
    CHECK(!shows_tile(ea->screen_x, ea->screen_y, 0x16));
    /* Within the margin of 3 nothing scrolls: B's cell, the source, is
     * redrawn with its icon, fallen or not. */
    int8_t vx = c->view_x;
    CHECK(cok_arena_centre(&game, eb->x, eb->y, 3, 0) && c->view_x == vx);
    CHECK(!shows_tile(eb->screen_x, eb->screen_y, 0x16));
    /* The cursor's box (icon slot 25) over the target's cells. */
    c->cursor = 1;
    CHECK(cok_arena_centre(&game, 27, 13, 3, 4));
    const cok_picture *box = &game.icons[25][0];
    for (int x = 0; x < 24; ++x)
        if (!masked(box, x, 0))
            CHECK(pixel(&game.screen, 0, 8 + ea->screen_x * 24 + x, 8 + (ea->screen_y + 1) * 24) ==
                  pixel(box, 0, x, 0));
    /* A target off the view and the map is clamped onto it (x 49). */
    c->view_x = 43;
    c->view_y = 9;
    CHECK(cok_arena_centre(&game, 52, 12, 3, 8) && c->view_x == 43);
    for (int x = 0; x < 24; ++x)
        if (!masked(box, x, 0))
            CHECK(pixel(&game.screen, 0, 8 + 6 * 24 + x, 8 + 3 * 24) == pixel(box, 0, x, 0));
    c->cursor = 0;
    /* A direction past the steps. */
    CHECK(!cok_arena_centre(&game, 27, 13, 3, 9) && strstr(game.error, "DS:1ed6") != NULL);
}

/* 6beb:0500 and 02aa: the terrain over a combatant's footprint from its
 * screen position as last worked out, or over one cell. */
static void test_erase(void)
{
    battle();
    const cok_combatant *ea = entry(1);
    CHECK(!shows_tile(ea->screen_x, ea->screen_y, 0x16));
    CHECK(cok_arena_erase(&game, 0, 0, 1));
    cok_arena_show(&game);
    CHECK(shows_tile(ea->screen_x, ea->screen_y, 0x16));
    CHECK(cok_arena_redraw_cell(&game, ea->x, ea->y));
    cok_arena_show(&game);
    CHECK(!shows_tile(ea->screen_x, ea->screen_y, 0x16));
    /* By its screen position, 0 finds the occupant there. */
    CHECK(cok_arena_erase(&game, ea->screen_x, ea->screen_y, 0));
    cok_arena_show(&game);
    CHECK(shows_tile(ea->screen_x, ea->screen_y, 0x16));
    /* Past the table. */
    CHECK(!cok_arena_erase(&game, 0, 0, 73));
}

/* 6beb:0ad8: the facing is kept whatever is shown; the icon is erased when
 * it turns to the other side, or for an image or hide, and drawn while the
 * actor's moves are shown. */
static void test_pose(void)
{
    battle();
    cok_character *a = game.party.members[0];
    const cok_combatant *ea = entry(1);
    game.combat.show_actions = false;
    CHECK(cok_arena_pose(&game, a, 2, 0, false) && a->combat->facing == 2);
    CHECK(s.pause_count == 0 && !shows_tile(ea->screen_x, ea->screen_y, 0x16));
    game.combat.show_actions = true;
    CHECK(cok_arena_pose(&game, a, 6, 0, true) && a->combat->facing == 6);
    cok_arena_show(&game);
    CHECK(shows_tile(ea->screen_x, ea->screen_y, 0x16));
    /* Attacking (image 1), facing east: CPIC1 129 as it is. */
    CHECK(cok_arena_pose(&game, a, 2, 1, false));
    const cok_picture *icon = &game.icons[8][1];
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            if (!masked(icon, x, y))
                CHECK(pixel(&game.screen, 0, 8 + ea->screen_x * 24 + x, 8 + ea->screen_y * 24 + y) ==
                      pixel(icon, 0, x, y));
    /* Back to ready on the same side: not erased, so where the ready
     * picture is clear the attacking one shows. */
    CHECK(cok_arena_pose(&game, a, 3, 0, false));
    const cok_picture *ready = &game.icons[8][0];
    unsigned left = 0;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            if (masked(ready, x, y) && !masked(icon, x, y) &&
                pixel(&game.screen, 0, 8 + ea->screen_x * 24 + x, 8 + ea->screen_y * 24 + y) ==
                    pixel(icon, 0, x, y) &&
                pixel(icon, 0, x, y) != pixel(&game.combat.tiles, 0x16, x, y))
                ++left;
    CHECK(left > 0);
    /* A record that is not a combatant. */
    cok_character outsider = {0};
    cok_combat_record cr = {0};
    outsider.combat = &cr;
    CHECK(!cok_arena_pose(&game, &outsider, 0, 0, false));
    CHECK(strstr(game.error, "6beb:0c43") != NULL);
}

/* 6beb:12ef: the cursor on in the combatant's footprint while centring,
 * then off with a footprint of 1. */
static void test_turn(void)
{
    battle();
    cok_combat *c = &game.combat;
    cok_character *d = game.party.members[3];
    d->record[0xcf] = 4;
    c->combatant[4].size = 4;
    CHECK(cok_combat_occupy(c));
    game.combat.show_actions = true;
    CHECK(cok_arena_turn(&game, d, 2, true));
    CHECK(c->cursor == 0 && c->cursor_size == 1);
    /* The box over the cell below the top-left too (footprint 4). */
    const cok_combatant *e = entry(4);
    const cok_picture *box = &game.icons[25][0];
    unsigned boxed = 0;
    for (int x = 0; x < 24; ++x)
        if (!masked(box, x, 0) &&
            pixel(&game.screen, 0, 8 + (e->screen_x + 1) * 24 + x, 8 + (e->screen_y + 1) * 24) ==
                pixel(box, 0, x, 0))
            ++boxed;
    CHECK(boxed > 0);
    /* Not shown: nothing drawn, the cursor still restored. */
    game.combat.show_actions = false;
    s.pause_count = 0;
    CHECK(cok_arena_turn(&game, d, 2, true) && c->cursor == 0 && c->cursor_size == 1);
    /* A record that is not a combatant: nothing to show, nothing refused;
     * shown, its place is not known. */
    cok_character *outside = record('E', 0, 8);
    CHECK(cok_combat_index(c, outside->record) == 0);
    CHECK(cok_arena_turn(&game, outside, 2, true) && c->cursor == 0 && c->cursor_size == 1);
    game.combat.show_actions = true;
    CHECK(!cok_arena_turn(&game, outside, 2, true) && strstr(game.error, "not a combatant") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
}

/* 6beb:0e08. */
static void test_kill(void)
{
    battle();
    cok_combat *c = &game.combat;
    cok_character *a = game.party.members[0], *cm = game.party.members[2];
    const cok_combatant *ea = entry(1);
    int8_t x = ea->x, y = ea->y;
    a->record[0x189] = 0;
    a->combat->initiative = 5;
    a->combat->movement = 6;
    a->combat->spell = 7;
    a->combat->guarding = 1;
    /* A pixel of the skull's where its two pictures differ. */
    const cok_picture *red = &game.icons[24][0], *pink = &game.icons[24][1];
    int sx = -1, sy = -1;
    for (int yy = 0; yy < 24 && sx < 0; ++yy)
        for (int xx = 0; xx < 24; ++xx)
            if (!masked(red, xx, yy) && !masked(pink, xx, yy) &&
                pixel(red, 0, xx, yy) != pixel(pink, 0, xx, yy)) {
                sx = xx, sy = yy;
                break;
            }
    CHECK(sx >= 0);
    s.sample_x = 8 + ea->screen_x * 24 + sx;
    s.sample_y = 8 + ea->screen_y * 24 + sy;
    CHECK(cok_arena_kill(&game, a, 0, NULL));
    s.sample_x = 0;
    /* Nine flashes of the skull 10 ms apart, ready then attacking, then
     * speed * 100 ms. */
    CHECK(s.pause_count == 10 && s.pauses[0] == 10 && s.pauses[8] == 10 && s.pauses[9] == 400);
    CHECK(s.samples[0] == pixel(red, 0, sx, sy) && s.samples[1] == pixel(pink, 0, sx, sy) &&
          s.samples[8] == pixel(red, 0, sx, sy));
    CHECK(strstr(s.log, "sound: 5;") != NULL);
    CHECK(c->bodies == 1 && c->body[0].character == a && c->body[0].x == x && c->body[0].y == y);
    CHECK(c->body[0].cell == COK_COMBAT_FLOOR && c->cells[y][x] == COK_COMBAT_BODY);
    CHECK(ea->size == 0 && c->occupant[y][x] == 0);
    CHECK(a->combat->initiative == 0 && a->combat->movement == 0 && a->combat->spell == 0 &&
          a->combat->guarding == 0);
    /* The body shows once the cell is redrawn. */
    CHECK(cok_arena_redraw_cell(&game, x, y));
    cok_arena_show(&game);
    CHECK(shows_tile(x - c->view_x, y - c->view_y, 0x27));
    /* Again, its stale byte 1 finding its body: nothing happens. */
    s.pause_count = 0;
    CHECK(cok_arena_kill(&game, a, 1, NULL) && s.pause_count == 0 && c->bodies == 1);
    /* With the byte 0 it is not found: a second body (the original's
     * search tests the wrong index), its cell the first body's. */
    CHECK(cok_arena_kill(&game, a, 0, NULL) && c->bodies == 2 && c->body[1].cell == COK_COMBAT_BODY);
    /* Unknown, the byte could find its bodies: the port stops. */
    CHECK(!cok_arena_kill(&game, a, COK_ARENA_STALE_UNKNOWN, NULL));
    CHECK(strstr(game.error, "stack") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    /* Past the table, but 0x24, the targets listed. */
    CHECK(!cok_arena_kill(&game, cm, 9, NULL) && strstr(game.error, "entry 9") != NULL);
    /* Bytes past the bodies that read the exploding list's entries 1, 8,
     * 15 (0x31, 0x35, 0x39) and the spell targets' 1, 8, ..., 64 (0xdb +
     * 4j): c there is found, and an unknown byte could find it. */
    cok_arena_reachable also = {0};
    also.exploding[1] = cm;
    s.pause_count = 0;
    CHECK(cok_arena_kill(&game, cm, 0x35, &also) && s.pause_count == 0);
    CHECK(!cok_arena_kill(&game, cm, COK_ARENA_STALE_UNKNOWN, &also) &&
          strstr(game.error, "stack") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    also.exploding[1] = NULL;
    also.targets[9] = cm;
    CHECK(cok_arena_kill(&game, cm, 0xff, &also) && s.pause_count == 0);
    CHECK(!cok_arena_kill(&game, cm, COK_ARENA_STALE_UNKNOWN, &also));
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    CHECK(!cok_arena_kill(&game, cm, 0xfe, &also) && strstr(game.error, "entry 254") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    /* A monster leaves no body; a green cloud stays under a body. */
    CHECK(cok_arena_kill(&game, cm, 0x24, NULL) && c->bodies == 2 && entry(3)->size == 0);
    cok_character *b = game.party.members[1];
    c->cells[entry(2)->y][entry(2)->x] = 0x1e;
    CHECK(cok_arena_kill(&game, b, COK_ARENA_STALE_UNKNOWN, NULL) && c->bodies == 3);
    CHECK(c->body[2].cell == 0x1e && c->cells[entry(2)->y][entry(2)->x] == 0x1e);
    /* A party member on the other side (+0x18a 1) leaves one too: only
     * +0x13 counts. */
    battle();
    game.party.members[0]->record[0x18a] = 1;
    CHECK(cok_arena_kill(&game, game.party.members[0], 0, NULL) && game.combat.bodies == 1);
    /* Setup clears only the count of bodies (3cb2:1cdd): an entry of an
     * earlier battle can still be found, by a byte that indexes it or one
     * not known. */
    battle();
    cok_character *first = game.party.members[0];
    first->record[0x189] = 0;
    CHECK(cok_arena_kill(&game, first, 0, NULL) && game.combat.bodies == 1);
    CHECK(cok_combat_end(&game));
    first->record[0x189] = 10;
    first->record[0x188] = 0;
    game.vm.mode = 4;
    CHECK(cok_combat_setup(&game) && game.combat.bodies == 0);
    CHECK(game.combat.body[0].character == first);
    CHECK(!cok_arena_kill(&game, first, COK_ARENA_STALE_UNKNOWN, NULL) && strstr(game.error, "stack") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    s.pause_count = 0;
    CHECK(cok_arena_kill(&game, first, 1, NULL) && s.pause_count == 0 && game.combat.bodies == 0);
    /* A ninth body would overrun the table. */
    battle();
    game.combat.bodies = 8;
    CHECK(!cok_arena_kill(&game, game.party.members[0], 0, NULL) && strstr(game.error, "ninth") != NULL);
    /* Outside combat only the sound and a pause. */
    battle();
    game.vm.mode = 4;
    CHECK(cok_arena_kill(&game, game.party.members[0], 0, NULL));
    CHECK(s.pause_count == 1 && s.pauses[0] == 400 && entry(1)->size == 1);
}

/* 6346:0af6, only when due. */
static void test_panel(void)
{
    battle();
    cok_character *a = game.party.members[0], *cm = game.party.members[2];
    CHECK(cok_arena_panel(&game, a) && blank(23, 1, 38, 21));
    game.combat.panel = true;
    a->record[0x197] = 7;  /* below its 10 */
    a->record[0x18d] = 62; /* AC -2 */
    CHECK(cok_arena_panel(&game, a) && !game.combat.panel);
    CHECK(shows_text("A", 23, 1, 11) && shows_text("Hitpoints", 23, 3, 10) &&
          shows_text("7", 33, 3, 14) && shows_text("AC", 23, 5, 10) && shows_text("-2", 26, 5, 10));
    CHECK(blank(23, 6, 38, 21));
    /* At its maximum, light green; AC 0 with no sign. */
    a->record[0x197] = a->record[0x62];
    a->record[0x18d] = 60;
    game.combat.panel = true;
    CHECK(a->record[0x62] == 10 && cok_arena_panel(&game, a));
    CHECK(shows_text("10", 33, 3, 10) && shows_text("0", 26, 5, 10) && blank(27, 5, 38, 5));
    /* An enemy's name in yellow, its weapon's name wrapped from row 7, its
     * status two rows below its last; "(Helpless)" or "(Casting)". */
    uint8_t sword[COK_ITEM_SIZE] = {0};
    sword[0x2e] = 0x12;
    sword[0x31] = 0x12;
    sword[0x34] = 1;
    CHECK(cok_character_insert_item(cm, 0, sword));
    cm->slots[0] = 1;
    cm->record[0x189] = 0;
    cm->record[0x188] = 6;
    game.combat.panel = true;
    CHECK(cok_arena_panel(&game, cm));
    CHECK(shows_text("C", 23, 1, 12) && shows_text("Long Sword", 23, 7, 10));
    CHECK(shows_text("Dead", 23, 9, 15));
    cm->record[0x189] = 1;
    CHECK(cok_character_add_effect(cm, 0x34, 0, 0, false) != NULL);
    game.combat.panel = true;
    CHECK(cok_arena_panel(&game, cm) && shows_text("C", 23, 1, 14));
    CHECK(shows_text("(Helpless)", 23, 9, 15));
    cok_effects_remove(&game.effects, cm, NULL, 0x34);
    cm->combat->spell = 3;
    game.combat.panel = true;
    CHECK(cok_arena_panel(&game, cm) && shows_text("(Casting)", 23, 9, 15));
    /* Without a weapon the status is on row 7; status 9 is the item name
     * after the statuses, "Battle Axe". */
    a->record[0x189] = 0;
    a->record[0x188] = 9;
    game.combat.panel = true;
    CHECK(cok_arena_panel(&game, a) && shows_text("Battle Axe", 23, 7, 15));
    a->record[0x188] = 0x80;
    game.combat.panel = true;
    CHECK(!cok_arena_panel(&game, a) && strstr(game.error, "DS:1330") != NULL);
}

/* 6346:1883 and 196a, in combat and outside it. */
static void test_say(void)
{
    battle();
    cok_character *a = game.party.members[0];
    /* The effects' text: row 10, a pause, then cleared. */
    s.pause_count = 0;
    game.effects.say(&game.effects, a, "is Unaffected", true, game.effects.context);
    CHECK(s.pause_count == 1 && s.pauses[0] == 400 && blank(23, 10, 38, 21));
    CHECK(strstr(s.log, "print: is Unaffected;") != NULL);
    s.pause_count = 0;
    CHECK(cok_arena_say(&game, a, "is hit", 10, false));
    CHECK(shows_text("A", 23, 10, 11) && shows_text("is hit", 23, 11, 10));
    CHECK(strstr(s.log, "print: A;print: is hit;") != NULL && s.pause_count == 0);
    CHECK(cok_arena_say(&game, a, "is hit", 10, true));
    CHECK(s.pause_count == 1 && s.pauses[0] == 400 && blank(23, 10, 38, 21));
    CHECK(cok_arena_say(&game, a, "is hit", 4, false));
    cok_arena_clear_text(&game);
    CHECK(shows_text("A", 23, 4, 11) && blank(23, 10, 38, 21));
    /* From row 20 the text has row 21; from 21 the original prints it over
     * the frame. */
    CHECK(cok_arena_say(&game, a, "is hit", 20, false) && shows_text("is hit", 23, 21, 10));
    CHECK(!cok_arena_say(&game, a, "is hit", 21, false) && strstr(game.error, "row 21") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.vm.mode = 4;
    CHECK(cok_arena_say(&game, a, "is hit", 21, false));
    CHECK(shows_text("A", 1, 18, 11) && shows_text("is hit", 1, 19, 10));
    cok_arena_clear_text(&game);
    CHECK(blank(1, 18, 38, 22));
}

/* 6346:228c. */
static void test_flash(void)
{
    battle();
    cok_character *cm = game.party.members[2];
    const cok_combatant *e = entry(3);
    game.speed = 2;
    CHECK(cok_arena_flash(&game, cm, 1, "is afraid"));
    /* Speed + 1 passes of four pictures 70 ms each, no pause after. */
    CHECK(s.pause_count == 12 && s.pauses[0] == 70 && s.pauses[11] == 70);
    CHECK(strstr(s.log, "sound: 4;print: C;print: is afraid;") != NULL);
    CHECK(shows_text("is afraid", 23, 11, 10));
    /* The map as it was: the monster's icon, erased each time. */
    const cok_picture *icon = &game.icons[10][0];
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            if (!masked(icon, x, y))
                CHECK(pixel(&game.screen, 0, 8 + e->screen_x * 24 + x, 8 + e->screen_y * 24 + y) ==
                      pixel(icon, 0, x, y));
    /* The pictures: slot 0x16's ready, mirrored, attacking mirrored and
     * attacking. */
    const cok_picture *f = &game.combat.flash, *ready = &game.icons[0x16][0],
                      *attack = &game.icons[0x16][1];
    unsigned asymmetric = 0;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) {
            CHECK(pixel(f, 0, x, y) == pixel(ready, 0, x, y));
            CHECK(pixel(f, 1, x, y) == pixel(ready, 0, 23 - x, y));
            CHECK(pixel(f, 2, x, y) == pixel(attack, 0, 23 - x, y));
            CHECK(pixel(f, 3, x, y) == pixel(attack, 0, x, y));
            asymmetric += pixel(attack, 0, x, y) != pixel(attack, 0, 23 - x, y) &&
                          pixel(ready, 0, x, y) != pixel(ready, 0, 23 - x, y);
        }
    CHECK(asymmetric > 0);
    /* Kind 0: slot 0x17, once, then speed * 100 ms; sound 3. */
    s.pause_count = 0;
    s.log[0] = '\0';
    CHECK(cok_arena_flash(&game, cm, 0, "is hit"));
    CHECK(s.pause_count == 5 && s.pauses[3] == 70 && s.pauses[4] == 200);
    CHECK(strncmp(s.log, "sound: 3;", 9) == 0);
    /* Outside combat it is said with a pause. */
    game.vm.mode = 4;
    s.pause_count = 0;
    CHECK(cok_arena_flash(&game, cm, 1, "is afraid") && s.pause_count == 1);
    /* The pictures must be 24 by 24: a large monster's are refused. */
    game.vm.mode = 5;
    CHECK(cok_arena_load_icon(&game, "CPIC", 11, 12) && game.icons[12][0].height == 48);
    CHECK(!cok_arena_missile_frames(&game, 12) && strstr(game.error, "24 by 24") != NULL);
    CHECK(cok_arena_missile_frames(&game, 13));
}

/* 6346:1ba6. */
static void test_missile(void)
{
    battle();
    cok_combat *c = &game.combat;
    CHECK(cok_arena_missile_frames(&game, 13));
    /* Both ends shown, three cells apart: nine steps of 8 pixels, the
     * last at the target, each shown 30 ms and erased. */
    CHECK(cok_arena_missile(&game, 25, 12, 28, 12, 4, 30));
    CHECK(s.pause_count == 9 && s.pauses[0] == 30 && shows_tile(4, 2, 0x16));
    /* With no delay only where it is on a cell's edge across or down:
     * along a row, at every step but the last; then it is left drawn at
     * the target. Diagonally, every third step. */
    s.pause_count = 0;
    CHECK(cok_arena_missile(&game, 25, 12, 28, 12, 4, 0));
    CHECK(s.pause_count == 8 && s.pauses[0] == 0 && !shows_tile(4, 2, 0x16));
    s.pause_count = 0;
    CHECK(cok_arena_missile(&game, 25, 12, 28, 15, 4, 0) && s.pause_count == 2);
    /* To its own cell, nothing. */
    s.pause_count = 0;
    CHECK(cok_arena_missile(&game, 25, 12, 25, 12, 4, 30) && s.pause_count == 0);
    /* Six cells apart, one off the view: centred on the midpoint. */
    battle();
    CHECK(cok_arena_missile_frames(&game, 13));
    CHECK(cok_arena_missile(&game, 27, 13, 33, 13, 4, 10) && c->view_x == 27 && c->view_y == 10);
    battle();
    CHECK(cok_arena_missile_frames(&game, 13));
    /* Far, from a cell shown to one off the view: the screen moves near
     * the target, centred on it, then the missile comes in. */
    s.pause_count = 0;
    CHECK(cok_arena_missile(&game, 27, 13, 45, 13, 4, 10));
    CHECK(c->view_x == 42 && c->view_y == 10);
    /* The step that leaves the view is drawn clipped and erased; when it is
     * the last (1ba6:1f6a), the target is drawn over what the erase left,
     * with no centring. The save (127f:27af) runs on in a row by the units
     * clipped on the right, not the left; the erase reads its rows as long
     * as the bytes drawn. So to 49, 2 from 44, 13 (with the view from 6, 1
     * first) the step saved at 19, 6, clipped on the right by a unit,
     * writes 8 bytes of each row of 12, and its erase reads rows of 8:
     * sheared, as the target's save over it shows. The same to 0, 2 from 5,
     * 13, clipped on the left at -1, 6, is not. The original's agree (p6diff,
     * and the review's emulator cases). */
    static const int8_t clipped[2][4] = {{44, 13, 49, 2}, {5, 13, 0, 2}};
    unsigned pauses = 0;
    for (int k = 0; k < 2; ++k) {
        battle();
        /* Cells of differing tiles where it flies, so that bytes drawn
         * from the wrong place show. */
        uint8_t values[8], found = 0;
        for (uint8_t v = 0; v < COK_COMBAT_TERRAINS && found < 8; ++v) {
            uint8_t tile = cok_combat_terrain[v].tile;
            if (tile < COK_COMBAT_TILES && (game.combat.tiles_loaded >> tile & 1) &&
                v != COK_COMBAT_FLOOR)
                values[found++] = v;
        }
        CHECK(found == 8);
        for (int yy = 0; yy < 6; ++yy)
            for (int xx = 0; xx < 4; ++xx)
                game.combat.cells[yy][k == 0 ? 49 - xx : xx] = values[(xx * 3 + yy) % 8];
        CHECK(cok_arena_centre(&game, k == 0 ? 6 : 43, 1, 0xff, 8));
        CHECK(cok_arena_missile_frames(&game, 13));
        s.pause_count = 0;
        CHECK(cok_arena_missile(&game, clipped[k][0], clipped[k][1], clipped[k][2], clipped[k][3],
                                4, 10));
        CHECK(s.pause_count >= 3);
        if (k == 0) pauses = s.pause_count;
        const uint8_t *before = s.unders[(s.pause_count - 3) % 4],
                      *cut = s.unders[(s.pause_count - 2) % 4],
                      *target = s.unders[(s.pause_count - 1) % 4];
        /* The map as drawn, for what the saves hold: the buffer redrawn. */
        CHECK(cok_arena_centre(&game, (int8_t)(c->view_x + 3), (int8_t)(c->view_y + 3), 0xff, 8));
        const uint8_t *map = game.combat.buffer.pixels;
        const size_t row = game.combat.buffer.units * 4;
        for (unsigned r = 0; r < 24; ++r)
            for (unsigned j = 0; j < 8; ++j) {
                if (k == 0) {
                    /* The save at 19, 6 by rows of 12, of which it wrote 8;
                     * the one at 18, 7 before it all 12. */
                    CHECK(cut[12 * r + j] == map[(48 + r) * row + 76 + j]);
                    if (j < 4) CHECK(cut[12 * r + 8 + j] == before[12 * r + 8 + j]);
                    CHECK(before[12 * r + j] == map[(56 + r) * row + 72 + j]);
                    /* The erase by rows of 8, sheared, under the target at
                     * 18 (its save's second unit). */
                    CHECK(target[12 * r + 4 + j] == cut[8 * r + j]);
                } else {
                    /* At -1, 6 by rows of 8, the erase the same: not
                     * sheared. */
                    CHECK(cut[8 * r + j] == map[(48 + r) * row + j]);
                    CHECK(target[12 * r + j] == map[(48 + r) * row + j]);
                }
            }
    }
    /* Bytes the erase reads that no save wrote (here as if none had, from
     * the clipped step's pause) are the heap's in the original: shown
     * through the target's transparent pixels, the port stops. */
    battle();
    CHECK(cok_arena_centre(&game, 6, 1, 0xff, 8));
    CHECK(cok_arena_missile_frames(&game, 13));
    s.forget = pauses - 1;
    CHECK(!cok_arena_missile(&game, 44, 13, 49, 2, 4, 10));
    CHECK(strstr(game.error, "no save has written") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    s.forget = 0;
    /* Unknown bytes saved stay unknown, and covered whole are known: with
     * the flash's pictures opaque, forgotten after its first, it is shown
     * through its last picture and stops only when erased at the end. */
    battle();
    game.speed = 1;
    for (int f = 0; f < 2; ++f) memset(game.icons[0x16][f].mask, 0, game.icons[0x16][f].frame_size);
    s.pause_count = 0;
    s.forget = 1;
    CHECK(!cok_arena_flash(&game, game.party.members[2], 1, "is hit"));
    CHECK(s.pause_count == 8 && strstr(game.error, "no save has written") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    s.forget = 0;
    game.speed = 4;
    CHECK(cok_arena_load_icon(&game, "COMSPR", 9, 0x16));
    /* Redrawn whole, the map shows again. */
    CHECK(cok_arena_centre(&game, (int8_t)(c->view_x + 3), (int8_t)(c->view_y + 3), 0xff, 8));
    /* Near the right edge (47 + 3 past 49): centred on 47 less 2, by the
     * original's arithmetic. */
    battle();
    CHECK(cok_arena_missile_frames(&game, 13));
    CHECK(cok_arena_missile(&game, 27, 13, 47, 13, 4, 10) && c->view_x == 42);
    /* To a target on an edge of the map (here the top row) from more than
     * 6 cells away, the walk back comes in where the
     * missile leaves again: the original never ends. */
    battle();
    CHECK(cok_arena_missile_frames(&game, 13));
    CHECK(!cok_arena_missile(&game, 43, 2, 5, 0, 4, 0));
    CHECK(strstr(game.error, "never lands") != NULL);
    /* From a cell above the view, level: drawn just above the buffer, which
     * the original's draw takes as 65536 rows. */
    battle();
    CHECK(cok_arena_missile_frames(&game, 13));
    CHECK(!cok_arena_missile(&game, 27, 9, 40, 9, 4, 10) && strstr(game.error, "65536") != NULL);
    /* A fifth picture is past the four. */
    battle();
    CHECK(cok_arena_missile_frames(&game, 13));
    CHECK(!cok_arena_missile(&game, 25, 12, 30, 12, 5, 10) && strstr(game.error, "four") != NULL);
}

/* 4b6d:0784. */
static void test_compose(void)
{
    cok_picture head = {0}, body = {0};
    CHECK(cok_picture_create(&head, 3, 2, 1, 1) == COK_PICTURE_OK);
    CHECK(cok_picture_create(&body, 3, 4, 1, 1) == COK_PICTURE_OK);
    head.pixels[0] = 0x12;
    head.mask[0] = 0x0f;
    head.mask[1] = 0xff;
    body.pixels[0] = 0x30;
    body.mask[0] = 0xf0;
    body.mask[1] = 0x0f;
    body.mask[30] = 0xff;
    CHECK(cok_arena_compose(&head, &body));
    CHECK(body.pixels[0] == 0x32 && body.mask[0] == 0x00 && body.mask[1] == 0x0f);
    CHECK(body.mask[30] == 0xff); /* past the head's bytes */
    /* A head larger than the body's frame, or a picture missing. */
    CHECK(!cok_arena_compose(&body, &head));
    cok_picture none = {0};
    CHECK(!cok_arena_compose(&none, &body));
    cok_picture_free(&head);
    cok_picture_free(&body);
}

/* Slot 2's icon is CHEAD head laid over CBODY body, colour 8 black,
 * colour 1 0xc and 9 2 (the colours of test_icons); how many pixels of
 * colour 1 there were. */
static unsigned check_party_icon(uint8_t head_id, uint8_t body_id)
{
    cok_picture head = {0}, body = {0};
    CHECK(cok_adventure_load_image(&game, "CHEAD", head_id, 0, &head));
    CHECK(cok_adventure_load_image(&game, "CBODY", body_id, 0, &body));
    const cok_picture *icon = &game.icons[2][0];
    unsigned ones = 0;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) {
            uint8_t want = pixel(&body, 0, x, y);
            if ((size_t)y < head.height) want = (uint8_t)(want | pixel(&head, 0, x, y));
            bool clear = masked(&body, x, y) && ((size_t)y >= head.height || masked(&head, x, y));
            CHECK(masked(icon, x, y) == clear);
            if (want == 8) want = 0;
            if (want == 1) want = 0xc, ++ones;
            else if (want == 9) want = 2;
            CHECK(pixel(icon, 0, x, y) == want);
        }
    cok_picture_free(&head);
    cok_picture_free(&body);
    return ones;
}

/* 4b6d:0817 and 6d21:01d0. */
static void test_icons(void)
{
    reset();
    cok_character *a = record('A', 0, 2);
    uint8_t *r = a->record;
    r[0x135] = 5;
    r[0x136] = 24;
    r[0x138] = 2;
    static const uint8_t colours[6] = {0x2c, 0xa2, 0xb3, 0xc4, 0xe6, 0xf7};
    memcpy(r + 0x139, colours, 6);
    CHECK(cok_arena_party_icon(&game, a, true));
    /* Large: CHEAD 0x45 over CBODY 0x58, colour 8 black, then colour 1 is
     * 0xc and 9 is 2. */
    CHECK(check_party_icon(0x45, 0x58) > 0);
    CHECK(game.icons[11][0].pixels == NULL && game.icons[2][1].pixels != NULL);
    /* Small (S): CHEAD 3 over CBODY 24 themselves. */
    r[0x138] = 1;
    r[0x135] = 3;
    CHECK(cok_arena_party_icon(&game, a, true));
    check_party_icon(3, 24);
    /* Size 0 is small with no letter; 3 reads past the letters. */
    r[0x138] = 0;
    r[0x135] = 13;
    CHECK(cok_arena_party_icon(&game, a, false));
    r[0x138] = 3;
    CHECK(!cok_arena_party_icon(&game, a, false) && strstr(game.error, "DS:0877") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    /* A head not in CHEAD leaves nothing to compose. */
    r[0x138] = 1;
    r[0x135] = 14;
    CHECK(!cok_arena_party_icon(&game, a, false) && strstr(game.error, "4b6d:0784") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    /* CPIC's colour 8 is drawn black, COMSPR's kept. */
    cok_picture raw = {0};
    unsigned found = 0;
    for (uint8_t id = 0; id < 0x80 && found == 0; ++id) {
        if (!cok_adventure_load_image(&game, "CPIC1", id, 0, &raw)) continue;
        for (int y = 0; y < raw.height && found == 0; ++y)
            for (int x = 0; x < raw.units * 8; ++x)
                if (pixel(&raw, 0, x, y) == 8) {
                    CHECK(cok_arena_load_icon(&game, "CPIC", id, 12));
                    CHECK(pixel(&game.icons[12][0], 0, x, y) == 0);
                    found = 1;
                    break;
                }
    }
    CHECK(found == 1);
    cok_picture_free(&raw);
    /* COMSPR's colour 8 stays: the skull (id 11, slot 24) has some. */
    unsigned grey = 0;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) grey += pixel(&game.icons[24][0], 0, x, y) == 8;
    CHECK(grey > 0);
    /* CHEADT: the T adds 0x40. A record not there leaves the slot empty. */
    CHECK(cok_arena_load_icon(&game, "CHEADT", 1, 12));
    CHECK(cok_adventure_load_image(&game, "CHEAD", 0x41, 0, &raw));
    CHECK(memcmp(raw.pixels, game.icons[12][0].pixels, raw.frame_size) == 0);
    cok_picture_free(&raw);
    CHECK(cok_arena_load_icon(&game, "CPIC", 0x7f, 12) && game.icons[12][0].pixels == NULL);
    CHECK(!cok_arena_load_icon(&game, "CPIC", 1, 26));
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    /* The party joining: an NPC's from CPIC of the file given. */
    reset();
    a = record('A', 0, 0);
    a->record[0x138] = 2;
    a->record[0x136] = 1;
    cok_character *n = record('N', 0, 1);
    n->record[0xe7] = 0x85;
    n->record[0x115] = 2; /* in CPIC2, not CPIC1 */
    CHECK(cok_arena_join(&game, 2));
    CHECK(cok_adventure_load_image(&game, "CPIC2", 2, 0, &raw));
    CHECK(game.icons[1][0].frame_size == raw.frame_size && game.icons[0][0].pixels != NULL);
    for (size_t i = 0; i < raw.frame_size; ++i) {
        uint8_t px = raw.pixels[i];
        if ((px & 0xf0) == 0x80) px &= 0x0f;
        if ((px & 0x0f) == 0x08) px &= 0xf0;
        CHECK(game.icons[1][0].pixels[i] == px);
    }
    cok_picture_free(&raw);
}

/* Effects that speak in combat flash in the panel (6346:228c) and clear
 * it (6346:196a), as a red dragon's fear does at setup. */
static void test_fear(void)
{
    reset();
    record('A', 0, 8);
    cok_character *dragon = record('D', 1, 10);
    game.vm.mem7c00[0x33e] = 1;
    game.vm.mem7c00[0x2c1] = 2;
    game.vm.character = cok_party_record(&game.party, 0);
    CHECK(cok_character_add_effect(dragon, 0x52, 0, 0xff, false) != NULL);
    CHECK(cok_combat_setup(&game));
    CHECK(strstr(s.log, "sound: 4;print: A;print: is terrified;") != NULL);
    CHECK(s.pause_count >= 21 && blank(23, 10, 38, 21));
    /* A flash that stops keeps its status and its one error: the effect
     * does not end the run again. */
    reset();
    record('A', 0, 8);
    dragon = record('D', 1, 10);
    game.vm.mem7c00[0x33e] = 1;
    game.vm.mem7c00[0x2c1] = 2;
    game.vm.character = cok_party_record(&game.party, 0);
    CHECK(cok_character_add_effect(dragon, 0x52, 0, 0xff, false) != NULL);
    cok_arena_free_icon(&game, 0x16);
    s.log[0] = '\0';
    CHECK(!cok_combat_setup(&game) && game.vm.status == COK_ECL_UNDEFINED);
    const char *first_error = strstr(s.log, "error: ");
    CHECK(first_error != NULL && strstr(first_error + 1, "error: ") == NULL);
    CHECK(strstr(game.error, "24 by 24") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    CHECK(cok_arena_load_icon(&game, "COMSPR", 9, 0x16));
}

/* The rounds draw where the original does: one that leaves the battle
 * (60f4:133c), the effects' flashes and 0x1e's side panel. */
static void test_rounds_drawn(void)
{
    battle();
    cok_combat *c = &game.combat;
    cok_character *a = game.party.members[0];
    const cok_combatant *e = entry(1);
    int8_t x = e->x, y = e->y, sx = e->screen_x, sy = e->screen_y;
    CHECK(cok_combat_leave(&game, a, 3, "Got Away"));
    CHECK(strstr(s.log, "print: A;print: Got Away;") != NULL);
    CHECK(s.pause_count == 1 && s.pauses[0] == 400 && blank(23, 10, 38, 21));
    CHECK(e->size == 0 && shows_tile(sx, sy, cok_combat_terrain[c->cells[y][x]].tile));
    /* A turn shows the combatant and draws its side panel. */
    battle();
    a = game.party.members[0];
    a->combat->initiative = 5;
    CHECK(cok_combat_turn(&game, a) && !game.combat.panel);
    CHECK(shows_text("A", 23, 1, 11) && shows_text("Hitpoints", 23, 3, 10));
    /* Snakes flash on the one fighting them, kind 1, and clear the text. */
    battle();
    a = game.party.members[0];
    game.speed = 1;
    cok_effect *snakes = cok_character_add_effect(a, 0x03, 0, 5, false);
    CHECK(snakes != NULL);
    CHECK(cok_effects_run(&game.effects, a, 0x03, snakes));
    CHECK(strstr(s.log, "sound: 4;print: A;print: is fighting with snakes;") != NULL);
    CHECK(s.pause_count == 8 && blank(23, 10, 38, 21));
    /* 0x1e draws the side panel for the selected, while it is due. */
    game.vm.character = a->record;
    game.combat.panel = true;
    s.pause_count = 0;
    a->combat->may_use = 1;
    CHECK(cok_effects_run(&game.effects, a, 0x1e, NULL) && !game.combat.panel);
    /* "is coughing" first, said with a pause (3f44:0b17). */
    CHECK(s.pause_count == 1 && s.pauses[0] == 100);
    CHECK(shows_text("A", 23, 1, 11));
    game.combat.panel = true;
    CHECK(cok_effects_run(&game.effects, game.party.members[1], 0x1e, NULL) && game.combat.panel);
    /* The stench says "emits an evil stench" with no wait (3f44:2b84). */
    battle();
    a = game.party.members[0];
    s.pause_count = 0;
    game.effects.in_battle = true;
    CHECK(cok_effects_run(&game.effects, a, 0x4f, NULL));
    game.effects.in_battle = false;
    CHECK(strstr(s.log, "print: emits an evil stench;") != NULL);
    CHECK(s.pause_count == 0 && shows_text("emits an evil", 23, 11, 10) &&
          shows_text("stench", 23, 12, 10));
    /* A flash that stops in a turn's event keeps its status and its one
     * error. */
    battle();
    a = game.party.members[0];
    game.effects.in_battle = true;
    CHECK(cok_character_add_effect(a, 0x23, 0, 0, false) != NULL);
    cok_arena_free_icon(&game, 0x16);
    cok_arena_free_icon(&game, 0x17);
    a->combat->initiative = 5;
    s.log[0] = '\0';
    CHECK(!cok_combat_turn(&game, a) && game.vm.status == COK_ECL_UNDEFINED);
    const char *error = strstr(s.log, "error: ");
    CHECK(error != NULL && strstr(error + 1, "error: ") == NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.effects.in_battle = false;
    CHECK(cok_arena_load_icon(&game, "COMSPR", 9, 0x16) && cok_arena_load_icon(&game, "COMSPR", 10, 0x17));
    battle();
    a = game.party.members[0];
    /* The flash hook clears the text only when asked: the stench's "is
     * affected" (3f44:2c65) stays. */
    CHECK(game.effects.flash(&game.effects, a, 1, "is affected", false, game.effects.context));
    CHECK(shows_text("is affected", 23, 11, 10));
    CHECK(game.effects.flash(&game.effects, a, 1, "is affected", true, game.effects.context));
    CHECK(blank(23, 10, 38, 21));
    game.speed = 4;
}

int main(void)
{
    cok_keyboard keys = {key, &s};
    cok_adventure_hooks hooks = {.log = log_line, .delay = delay, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    /* Startup loads COMSPR's icons into slots 13-25 (3e99:06e0). */
    CHECK(game.icons[13][0].pixels != NULL && game.icons[24][1].pixels != NULL &&
          game.icons[25][0].pixels != NULL);
    test_tables();
    test_frame();
    test_map();
    test_off_the_map();
    test_centre();
    test_erase();
    test_pose();
    test_turn();
    test_kill();
    test_panel();
    test_say();
    test_flash();
    test_missile();
    test_compose();
    test_icons();
    test_fear();
    test_rounds_drawn();
    cok_adventure_close(&game);
    puts("arena tests passed");
    return 0;
}
