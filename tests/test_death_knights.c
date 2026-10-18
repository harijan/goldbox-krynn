/* Death Knights of Krynn's descriptor (src/rules_death_knights.c): what it
 * holds against the original's bytes in build/death-START_FULL.EXE (make
 * death-merged) and against the files in DKKAssets/; its 3D view's wall
 * sets and tiles (view.c with its shape); and what it leaves out. Without
 * DKKAssets/ the checks of its files and executable are skipped, with a
 * line saying so. */
#include "dax.h"
#include "rules.h"
#include "view.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_death_knights;

static void test_lookup(void)
{
    CHECK(cok_rules_find("death-knights") == R);
    CHECK(strcmp(cok_rules_names(), "champions, death-knights") == 0);
    CHECK(strcmp(R->assets, "DKKAssets") == 0);
    CHECK(strcmp(R->text.name, "Death Knights of Krynn") == 0);
    CHECK(R->unplayable != NULL && cok_rules_champions.unplayable == NULL);
    /* Left out: the character record and all but two of the data
     * segment's tables, whose accessors then assert. */
    CHECK(R->character->size == 0 && R->character->file_size == 0);
    for (size_t f = 0; f < COK_FIELD_COUNT; ++f) CHECK(R->character->field[f].width == 0);
    for (size_t t = 0; t < COK_DATA_TABLES; ++t)
        CHECK((R->data->table[t].width != 0) ==
              (t == COK_DATA_SAME_COLOURS || t == COK_DATA_BLACK_COLOURS));
    /* The archives Death Knights has not. */
    const cok_rules_archives *a = &R->archives;
    CHECK(a->sky == NULL && a->head == NULL && a->body == NULL && a->random == NULL);
    CHECK(a->music_drivers == NULL && a->music_score == NULL && R->saves.debug == NULL);
    CHECK(cok_rules_champions.archives.ground == NULL &&
          cok_rules_champions.archives.backdrop == NULL &&
          cok_rules_champions.archives.wall_files == NULL);
    CHECK(a->file_digit(1) == '1' && a->file_digit(3) == '1');
}

static bool exists(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "DKKAssets/%s", name);
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Whether DKKAssets/<name> holds record id. */
static bool holds(const char *name, unsigned id)
{
    char path[256];
    snprintf(path, sizeof path, "DKKAssets/%s", name);
    dax_archive archive = {0};
    if (dax_open(path, &archive) != DAX_OK) return false;
    bool found = false;
    for (size_t i = 0; i < archive.count; ++i) {
        dax_record record;
        CHECK(dax_record_at(&archive, i, &record) == DAX_OK);
        found |= record.id == id;
    }
    dax_close(&archive);
    return found;
}

static void test_assets(void)
{
    const cok_rules_archives *a = &R->archives;
    char name[64];
    const char *const numbered[] = {a->ecl, a->geo, a->walldef, a->tiles, a->pic,
                                    a->sprite, a->big, a->icons, a->backdrop};
    for (size_t i = 0; i < sizeof numbered / sizeof *numbered; ++i) {
        snprintf(name, sizeof name, "%s1.DAX", numbered[i]);
        CHECK(exists(name));
    }
    snprintf(name, sizeof name, "%s1%s.DAX", a->monster, a->monster_record);
    CHECK(exists(name));
    const char *const singles[] = {a->missiles, a->icon_head, a->icon_body, a->ground,
                                   a->title,    a->dungeon,   a->wilderness};
    for (size_t i = 0; i < sizeof singles / sizeof *singles; ++i) {
        snprintf(name, sizeof name, "%s.DAX", singles[i]);
        CHECK(exists(name));
    }
    /* Champions' files it has not. */
    CHECK(!exists("SKY.DAX") && !exists("CURSOR.DAX") && !exists("RANDCOM.DAX"));
    CHECK(!exists("HEAD1.DAX") && !exists("BODY1.DAX") && !exists("TILES.DAX"));
    CHECK(exists(R->files.config) && exists(R->files.item_types));
    /* The font, the cursor and the startup's tile set; the ground. */
    CHECK(holds("8X8D1.DAX", R->archives.font.record) && R->archives.font.record == 201);
    CHECK(R->archives.cursor.file == 1 && R->archives.cursor.record == 204);
    CHECK(holds("8X8D1.DAX", R->archives.cursor.record));
    for (size_t i = 0; i < R->startup.tile_count; ++i)
        CHECK(holds("8X8D1.DAX", R->startup.tiles[i].record));
    CHECK(holds("SKYGRND.DAX", 1) && holds("SKYGRND.DAX", 2));
    /* Each wall set by its id: WALLDEF, 8X8D and BACK of the file its id
     * names hold it, and no other file does. */
    for (unsigned id = 0; id < 256; ++id) {
        unsigned file = a->wall_files[id];
        for (unsigned n = 1; n <= 5; ++n) {
            const char *const stems[] = {a->walldef, a->tiles, a->backdrop};
            for (size_t s = 0; s < 3; ++s) {
                snprintf(name, sizeof name, "%s%u.DAX", stems[s], n);
                if (s == 1 && n == 1 && (id == 201 || id == 202 || id == 203 || id == 204))
                    continue; /* the font's file */
                CHECK(holds(name, id) == (file == n));
            }
        }
    }
    /* Each picture and sprite of PIC4-7 and SPRIT4 by its id, which the
     * ECL file does not choose. */
    for (unsigned id = 0; id < 256; ++id) {
        for (unsigned n = 4; n <= 7; ++n) {
            snprintf(name, sizeof name, "%s%u.DAX", a->pic, n);
            CHECK(holds(name, id) == (a->pic_files[id] == n));
        }
        snprintf(name, sizeof name, "%s4.DAX", a->sprite);
        CHECK(holds(name, id) == (a->sprite_files[id] == 4));
        CHECK(a->sprite_files[id] == 0 || a->sprite_files[id] == 4);
    }
    /* The archives the image tools export. */
    const char *const *lists[] = {R->images.delta, R->images.collection, R->images.single};
    size_t listed = 0;
    for (size_t i = 0; i < 3; ++i)
        for (const char *const *stem = lists[i]; *stem != NULL; ++stem, ++listed) {
            snprintf(name, sizeof name, "%s.DAX", *stem);
            CHECK(exists(name));
        }
    CHECK(listed == 30);
}

/* The original's bytes: data segment DS 0x1bce (image paragraph 0x0bce). */
static uint8_t exe[1 << 20];
static size_t exe_size, header;

static const uint8_t *at(unsigned segment, unsigned offset)
{
    size_t where = header + (size_t)(segment - 0x1000) * 16 + offset;
    CHECK(where < exe_size);
    return exe + where;
}

static const uint8_t *ds(unsigned offset)
{
    return at(0x1bce, offset);
}

static void test_executable(void)
{
    FILE *f = fopen("build/death-START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("death knights: build/death-START_FULL.EXE not built (make death-merged); "
             "its tables not checked");
        return;
    }
    exe_size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(exe_size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    header = (size_t)(exe[8] | exe[9] << 8) * 16;
    /* The data segment's tables, byte for byte. */
    const cok_data_segment *D = R->data;
    for (size_t i = 0; i < D->table_count; ++i) {
        const cok_ds_table *t = &D->tables[i];
        for (unsigned k = 0; k < t->size; ++k) CHECK(*ds(t->offset + k * t->stride) == t->bytes[k]);
    }
    for (unsigned o = 0; o <= 0xffff; ++o) {
        uint8_t byte;
        if (cok_data_byte(D, (uint16_t)o, &byte)) CHECK(*ds(o) == byte);
    }
    /* The tile sets' first values and bounds (DS:43fa, DS:4404, 70eb:01f5). */
    for (size_t s = 0; s < 5; ++s) {
        CHECK(R->view.tile_first[s] == (ds(0x43fa)[2 * s] | ds(0x43fa)[2 * s + 1] << 8));
        CHECK(R->view.tile_last[s] == (ds(0x4404)[2 * s] | ds(0x4404)[2 * s + 1] << 8));
    }
    /* The wall sets' files by id: 6d21:0ec5's strings (and 70eb:002f's,
     * the same digits) for ids 1, 2, 3-4, 5-6, 7. */
    static const struct {
        uint8_t id;
        unsigned offset;
    } digits[] = {{1, 0}, {2, 2}, {3, 4}, {4, 4}, {5, 6}, {6, 6}, {7, 8}};
    for (size_t i = 0; i < sizeof digits / sizeof *digits; ++i) {
        const uint8_t *s = at(0x6d21, 0x0eb1 + digits[i].offset);
        const uint8_t *t = at(0x70eb, 0x0020 + digits[i].offset);
        CHECK(s[0] == 1 && t[0] == 1 && s[1] == t[1]);
        CHECK(R->archives.wall_files[digits[i].id] == (unsigned)(s[1] - '0'));
    }
    const uint8_t *walldef = at(0x6d21, 0x0ebb), *back = at(0x6d21, 0x0e49);
    CHECK(walldef[0] == 7 && memcmp(walldef + 1, "WALLDEF", 7) == 0);
    CHECK(back[0] == 4 && memcmp(back + 1, "back", 4) == 0);
    /* SPRIT4's ids: the set at 6ca1:006e. */
    const uint8_t *set = at(0x6ca1, 0x006e);
    for (unsigned id = 0; id < 256; ++id)
        CHECK((R->archives.sprite_files[id] == 4) == ((set[id / 8] >> (id % 8) & 1) != 0));
    /* MON's and CPIC's file digit, always 1 (46b2:198c, 7090:016d). */
    CHECK(memcmp(at(0x46b2, 0x198c), "\x01" "1" "\x03" "MON", 6) == 0);
    CHECK(memcmp(at(0x7090, 0x016d), "\x01" "1", 2) == 0);
    CHECK(R->archives.file_digit(2) == '1');
    /* The flags pictures load with: DS:42d0 (the ground's, the backdrops',
     * the cursor's) builds no mask, DS:42d1 (the tile sets') does. */
    CHECK(*ds(0x42d0) == 0 && *ds(0x42d1) == 1 && R->startup.cursor_transparent == -1);
    /* Startup's file names (4f9a:00c6-00db). */
    const uint8_t *names = at(0x4f9a, 0x00c6);
    CHECK(memcmp(names, "\x06" "COMSPR" "\x05" "8x8d1" "\x07" "skygrnd" "\x05" "ITEMS", 27) == 0);
    /* The version string. */
    const char *version = R->text.version;
    bool found = false;
    for (size_t i = 0; i + strlen(version) < exe_size && !found; ++i)
        found = exe[i] == strlen(version) && memcmp(exe + i + 1, version, strlen(version)) == 0;
    CHECK(found);
    printf("death knights: %zu tables, tile sets, wall and sprite files checked against "
           "build/death-START_FULL.EXE\n", D->table_count);
}

/* Wall sets by slot, with the shape's first slot and no tiles moved; tile
 * values by the tile sets' bounds. */
static void test_view(void)
{
    static uint8_t record[3 * COK_VIEW_WALL_SET];
    for (size_t i = 0; i < sizeof record; ++i) record[i] = (uint8_t)(i * 7 + 0x30);
    cok_view view = {.shape = &R->view};
    CHECK(cok_view_set_walls(&view, 0, record, sizeof record) == 3);
    CHECK(memcmp(view.walls, record, sizeof record) == 0); /* nothing moved */
    CHECK(cok_view_set_walls(&view, 1, record, sizeof record) == 0); /* past the sets */
    CHECK(cok_view_set_walls(&view, 1, record, COK_VIEW_WALL_SET) == 1);
    CHECK(memcmp(view.walls + COK_VIEW_WALL_SET, record, COK_VIEW_WALL_SET) == 0);
    CHECK(cok_view_set_walls(&view, 4, record, COK_VIEW_WALL_SET) == 0);
    /* Champions': slot 1 is the first, values from 0x2d moved by slot 2's
     * offset. */
    cok_view champions = {.shape = &cok_rules_champions.view};
    CHECK(cok_view_set_walls(&champions, 0, record, COK_VIEW_WALL_SET) == 0);
    CHECK(cok_view_set_walls(&champions, 2, record, COK_VIEW_WALL_SET) == 1);
    CHECK(champions.walls[COK_VIEW_WALL_SET] == (uint8_t)(record[0] + 0x74 - 0x2e));

    /* A tile set of 256 tiles (a header's count 0xff) and the frame's: each
     * value from its set. */
    cok_picture screen = {0}, tiles = {0}, frame = {0};
    CHECK(cok_picture_create(&screen, 4, 8, 1, 0) == COK_PICTURE_OK);
    CHECK(cok_picture_create(&tiles, 1, 8, 256, 0) == COK_PICTURE_OK);
    CHECK(cok_picture_create(&frame, 1, 8, 45, 0) == COK_PICTURE_OK);
    for (size_t f = 0; f < 256; ++f) memset(tiles.pixels + f * 32, (int)(f & 15) * 0x11, 32);
    for (size_t f = 0; f < 45; ++f) memset(frame.pixels + f * 32, (int)((f + 3) & 15) * 0x11, 32);
    view.tiles[0] = tiles;
    view.tiles[4] = frame;
    cok_view_tile(&screen, &view, 0x4e, 0, 0, false);  /* set 0, tile 0x4e */
    cok_view_tile(&screen, &view, 0x110, 1, 0, false); /* the frame's 0x10 */
    cok_view_tile(&screen, &view, 0x12d, 2, 0, false); /* past the sets: nothing */
    cok_view_tile(&screen, &view, 0xff, 3, 0, false);  /* the 256th */
    CHECK(screen.pixels[0] == 0xee && screen.pixels[4] == 0x33 && screen.pixels[8] == 0);
    CHECK(screen.pixels[12] == 0xff);
    cok_picture_free(&screen);
    cok_picture_free(&tiles);
    cok_picture_free(&frame);

    /* The backdrop (6d21:0177): on a square below 0x80 the ground by day,
     * past hour 13 the night sky over it; on one of 0x80 and up the wall
     * set's; each opaque over the fills. */
    cok_view sky = {.shape = &R->view};
    sky.wrap = false;
    sky.map[0x200 + 5 * 16 + 5] = 0x10;
    sky.map[0x200 + 6 * 16 + 5] = 0x90;
    cok_picture *pictures[] = {&sky.ground[0], &sky.ground[1], &sky.back};
    const int colours[] = {2, 4, 6}, rows[] = {88, 44, 88};
    for (size_t i = 0; i < 3; ++i) {
        CHECK(cok_picture_create(pictures[i], 11, (size_t)rows[i], 1, 0) == COK_PICTURE_OK);
        memset(pictures[i]->pixels, colours[i] * 0x11, pictures[i]->frame_size);
    }
    CHECK(cok_picture_create(&screen, 40, 200, 1, 0) == COK_PICTURE_OK);
    /* The colour at the view's top and bottom row (cells 3 and 13). */
    struct {
        int y, hour, top, bottom;
    } cases[] = {{5, 13, 2, 2}, {5, 14, 4, 2}, {6, 14, 6, 6}};
    for (size_t i = 0; i < 3; ++i) {
        cok_view_backdrop b = {.sky = 11, .ground = 8, .hour = (uint16_t)cases[i].hour};
        cok_view_draw(&screen, &sky, 5, cases[i].y, 0, &b);
        CHECK(screen.pixels[3 * 8 * 160 + 3 * 4] >> 4 == cases[i].top);
        CHECK(screen.pixels[(13 * 8 + 7) * 160 + 3 * 4] >> 4 == cases[i].bottom);
    }
    cok_picture_free(&screen);
    cok_view_free(&sky);
}

int main(void)
{
    test_lookup();
    test_view();
    struct stat st;
    if (stat("DKKAssets/START.EXE", &st) != 0) {
        puts("death knights: DKKAssets not found; its files' and executable's checks skipped");
    } else {
        test_assets();
        test_executable();
    }
    puts("death knights: ok");
    return 0;
}
