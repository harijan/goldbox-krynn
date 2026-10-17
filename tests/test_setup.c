#define _POSIX_C_SOURCE 200809L /* mkdtemp */

/* Startup's text screens (src/setup.c, src/crt.c): the setup and the note
 * to Tandy users against what the original's 11c7:031a, Crt and System
 * left in an 8086 emulator (tests/test_setup_data.h): the text screen and
 * cursor at each key read and delay, and the file; then the text screen's
 * scrolling, logging, rendering and font, and the files cok_setup_startup
 * reads and writes. */
#include "setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    int kind; /* 0 a key read, 1 a delay, 2 the end */
    unsigned value, x, y;
    uint32_t digest;
} setup_event;

typedef struct {
    const char *game_dir;
    int mode; /* 0 the setup, 1 the file cannot be made, 2 the file exists */
    const int (*keys)[2];
    size_t key_count;
    const setup_event *events;
    size_t event_count;
    const char *file;
    size_t file_size;
    int end; /* 0 done, 1 out of keys, 2 halted */
} setup_case;

#include "test_setup_data.h"
#include "test_temp.h"

static uint32_t fnv(const uint8_t *bytes, size_t size)
{
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < size; ++i) h = (h ^ bytes[i]) * 0x01000193u;
    return h;
}

/* A replay: the case's keys, each read and delay checked against the
 * original's next event. */
typedef struct {
    const setup_case *c;
    cok_crt crt;
    size_t key, event;
    int owed;
    unsigned case_index;
} replay;

static replay r;

static void expect(int kind, unsigned value)
{
    uint8_t cells[4000];
    for (int y = 0; y < 25; ++y)
        for (int x = 0; x < 80; ++x) {
            cells[(y * 80 + x) * 2] = r.crt.chars[y][x];
            cells[(y * 80 + x) * 2 + 1] = r.crt.attrs[y][x];
        }
    if (r.event >= r.c->event_count) {
        fprintf(stderr, "case %u: more events than the original's %zu\n", r.case_index,
                r.c->event_count);
        exit(1);
    }
    const setup_event *e = &r.c->events[r.event++];
    if (e->kind != kind || e->value != value || e->x != (unsigned)r.crt.x ||
        e->y != (unsigned)r.crt.y || e->digest != fnv(cells, sizeof cells)) {
        fprintf(stderr, "case %u event %zu: kind %d %u at %d,%d %08x, the original's %d %u at "
                        "%u,%u %08x\n",
                r.case_index, r.event - 1, kind, value, r.crt.x, r.crt.y,
                fnv(cells, sizeof cells), e->kind, e->value, e->x, e->y, e->digest);
        exit(1);
    }
}

static int read_key(void *context)
{
    (void)context;
    if (r.owed >= 0) {
        int scan = r.owed;
        r.owed = -1;
        return scan;
    }
    expect(0, 0);
    if (r.key == r.c->key_count) return -1;
    const int *k = r.c->keys[r.key++];
    if (k[0] == 0) r.owed = k[1];
    return k[0];
}

static void delay(unsigned ms, void *context)
{
    (void)context;
    expect(1, ms);
}

static void test_cases(void)
{
    size_t count = sizeof setup_cases / sizeof setup_cases[0];
    unsigned written = 0, ended = 0, defaulted = 0;
    for (size_t i = 0; i < count; ++i) {
        r = (replay){.c = setup_cases[i], .owed = -1, .case_index = (unsigned)i};
        cok_crt_init(&r.crt);
        cok_keyboard keys = {read_key, NULL, NULL, NULL};
        cok_setup_hooks hooks = {delay, NULL, NULL, NULL, false};
        uint8_t file[COK_SETUP_FILE_MAX];
        size_t size = 0;
        int end = 0;
        if (r.c->mode == 1) {
            cok_setup_write_protected(&r.crt);
            end = 2;
        } else {
            bool asked = r.c->mode == 2 ||
                         cok_setup_ask(&r.crt, &keys, &hooks, &cok_rules_champions, r.c->game_dir,
                                       file, &size);
            if (asked)
                cok_setup_numlock(&r.crt, &hooks);
            else
                end = 1;
        }
        expect(2, 0);
        CHECK(r.event == r.c->event_count);
        CHECK(end == r.c->end);
        if (end == 0 && r.c->mode == 0) {
            CHECK(size == r.c->file_size && memcmp(file, r.c->file, size) == 0);
            ++written;
            if (r.c->events[r.c->event_count - 3].kind == 1) ++defaulted; /* USING DEFAULT PATH */
        }
        ended += end == 1;
    }
    /* The cases reach what they are meant to. */
    CHECK(written > 40 && ended > 3 && defaulted > 20 && defaulted < written);
}

/* The file's lines past the sound, read as startup reads them on. */
static void test_parse(void)
{
    const char *assets = "E\r\nT\r\nM\r\nC:\\COK\\SAVE\\\r\nF\r\n0\r\n0\r\n";
    cok_config c = cok_config_parse((const uint8_t *)assets, strlen(assets));
    CHECK(c.mouse && !c.joystick && strcmp(c.save_path, "C:\\COK\\SAVE\\") == 0 && c.f_line);
    const char *joy = "C\r\nP\r\nJ\r\nD:\\X\\\r\nG\r\n1\r\n-2\r\n$1f\r\n+4\r\n 5 \r\n6\r\n7\r\n8\r\n"
                      "9\r\n10\r\n65535\r\n12\r\n13\r\n14\r\n";
    c = cok_config_parse_stick((const uint8_t *)joy, strlen(joy), true);
    CHECK(c.joystick && !c.mouse && !c.f_line && strcmp(c.save_path, "D:\\X\\") == 0);
    CHECK(c.joystick_x[0] == 1 && c.joystick_y[0] == 0xfffe && c.joystick_x[1] == 0x1f &&
          c.joystick_y[1] == 4 && c.joystick_x[2] == 5 && c.joystick_y[6] == 14 &&
          c.joystick_x[5] == 0xffff);
    /* Without a stick (the port's machine) 177c:0000 turns the joystick
     * off and nothing is read. */
    c = cok_config_parse((const uint8_t *)joy, strlen(joy));
    CHECK(!c.joystick && !c.mouse && c.joystick_x[0] == 0 && c.joystick_y[6] == 0 &&
          strcmp(c.save_path, "D:\\X\\") == 0);
    /* Blanks and control characters before a number are skipped; a Ctrl-Z
     * is not (it ends the reads), nor does a string take it. */
    const char *blanks = "C\r\nP\r\nJ\r\nD:\\\r\nF\r\n \t\x01" "7\r\n\x1a" "8\r\n";
    c = cok_config_parse_stick((const uint8_t *)blanks, strlen(blanks), true);
    CHECK(c.joystick_x[0] == 7 && c.joystick_y[0] == 0);
    const char *ended = "E\r\nT\r\nN\r\nD:\x1aX\r\n";
    c = cok_config_parse((const uint8_t *)ended, strlen(ended));
    CHECK(strcmp(c.save_path, "D:") == 0);
    /* Hexadecimal past 32 bits is the error. */
    const char *hex = "C\r\nP\r\nJ\r\nD:\\\r\nF\r\n$100000000\r\n5\r\n";
    c = cok_config_parse_stick((const uint8_t *)hex, strlen(hex), true);
    CHECK(c.joystick_x[0] == 0 && c.joystick_y[0] == 0);
    /* Read(f, integer)'s error 106 stops every later read. */
    const char *bad = "C\r\nP\r\nJ\r\nD:\\\r\nF\r\n1\r\n2x\r\n3\r\n";
    c = cok_config_parse_stick((const uint8_t *)bad, strlen(bad), true);
    CHECK(c.joystick_x[0] == 1 && c.joystick_y[0] == 0 && c.joystick_x[1] == 0);
    const char *x_bad = "C\r\nP\r\nJ\r\nD:\\\r\nF\r\n1\r\n2\r\nzz\r\n4\r\n";
    c = cok_config_parse_stick((const uint8_t *)x_bad, strlen(x_bad), true);
    CHECK(c.joystick_x[0] == 1 && c.joystick_y[0] == 2 && c.joystick_x[1] == 0 &&
          c.joystick_y[1] == 0);
    /* More than a longint holds is the error too. */
    const char *big = "C\r\nP\r\nJ\r\nD:\\\r\nF\r\n2147483648\r\n5\r\n";
    c = cok_config_parse_stick((const uint8_t *)big, strlen(big), true);
    CHECK(c.joystick_x[0] == 0 && c.joystick_y[0] == 0);
    const char *biggest = "C\r\nP\r\nJ\r\nD:\\\r\nF\r\n2147483647\r\n$7fffffff\r\n$80000000\r\n5\r\n";
    c = cok_config_parse_stick((const uint8_t *)biggest, strlen(biggest), true);
    CHECK(c.joystick_x[0] == 0xffff && c.joystick_y[0] == 0xffff && c.joystick_x[1] == 0 &&
          c.joystick_y[1] == 5);
    const char *neither = "C\r\nP\r\nN\r\n";
    c = cok_config_parse((const uint8_t *)neither, strlen(neither));
    CHECK(!c.mouse && !c.joystick && c.save_path[0] == '\0' && !c.f_line);
    /* Anything else leaves both as they were: off. */
    const char *other = "C\r\nP\r\nm\r\n";
    c = cok_config_parse((const uint8_t *)other, strlen(other));
    CHECK(!c.mouse && !c.joystick);
    /* The path: up to the CR, at most 255 characters; an empty line 3 is
     * Read's CR, and its ReadLn skips line 4. */
    char longer[400] = "E\r\nT\r\nN\r\n";
    for (int i = 0; i < 300; ++i) strcat(longer, "x");
    strcat(longer, "\r\nF\r\n");
    c = cok_config_parse((const uint8_t *)longer, strlen(longer));
    CHECK(strlen(c.save_path) == 255 && c.f_line);
    const char *skip = "E\r\nT\r\n\r\nPATH\r\nF\r\n";
    c = cok_config_parse((const uint8_t *)skip, strlen(skip));
    CHECK(strcmp(c.save_path, "F") == 0 && !c.f_line);
}

static void test_default_path(void)
{
    char path[80];
    cok_setup_default_path(&cok_rules_champions, "C:\\COK\\", path, sizeof path);
    CHECK(strcmp(path, "C:\\COK\\SAVE") == 0);
    cok_setup_default_path(&cok_rules_champions, "A:\\", path, sizeof path);
    CHECK(strcmp(path, "B:\\SAVE") == 0);
    cok_setup_default_path(&cok_rules_champions, "B:\\GAME\\", path, sizeof path);
    CHECK(strcmp(path, "A:\\SAVE") == 0);
}

static char logged[64][81];
static size_t log_count;

static void log_row(const char *row, void *context)
{
    (void)context;
    CHECK(log_count < 64);
    snprintf(logged[log_count++], sizeof logged[0], "%s", row);
}

static void test_crt(void)
{
    static cok_crt crt;
    crt.log = log_row;
    cok_crt_init(&crt);
    CHECK(crt.chars[24][79] == ' ' && crt.attrs[24][79] == 7 && crt.x == 0 && crt.y == 0);
    /* Wrap past the last column; scroll past the last row. */
    for (int i = 0; i < 25; ++i) {
        char line[16];
        snprintf(line, sizeof line, "row %d", i);
        cok_crt_write_line(&crt, line);
    }
    CHECK(crt.y == 24 && crt.x == 0 && memcmp(crt.chars[0], "row 1 ", 6) == 0 &&
          memcmp(crt.chars[23], "row 24", 6) == 0 && crt.chars[24][0] == ' ');
    CHECK(log_count == 25 && strcmp(logged[0], "row 0") == 0 && strcmp(logged[24], "row 24") == 0);
    cok_crt_clear(&crt);
    char wide[90];
    memset(wide, 'w', 85);
    wide[85] = '\0';
    cok_crt_write_string(&crt, wide);
    CHECK(crt.y == 1 && crt.x == 5 && crt.chars[1][4] == 'w' && log_count == 26);
    cok_crt_write_string(&crt, "\b\b\a\r");
    CHECK(crt.x == 0 && crt.y == 1);
    cok_crt_write_string(&crt, "\b");
    CHECK(crt.x == 0);
    cok_crt_log_rows(&crt);
    CHECK(log_count == 27 && strcmp(logged[26], "wwwww") == 0);
    cok_crt_log_rows(&crt);
    CHECK(log_count == 27);
    /* The last column of the last row scrolls at once. */
    cok_crt_clear(&crt);
    for (int i = 0; i < 24; ++i) cok_crt_write_string(&crt, "\n");
    memset(wide, 'z', 80);
    wide[80] = '\0';
    cok_crt_write_string(&crt, wide);
    CHECK(crt.y == 24 && crt.x == 0 && crt.chars[23][79] == 'z' && crt.chars[24][0] == ' ');

    /* Rendering: glyphs in the attribute's colours, the cursor's lines. */
    cok_crt_clear(&crt);
    crt.attr = 0x1e;
    cok_crt_write_string(&crt, "Aa\x7f\xc4");
    static uint8_t pixels[COK_CRT_WIDTH * COK_CRT_HEIGHT / 2];
    cok_crt_render(&crt, pixels);
    /* A's rows 0 and 4: ...##... and .######., yellow on blue. */
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x1e && pixels[2] == 0xe1 && pixels[3] == 0x11);
    CHECK(pixels[4 * 320] == 0x1e && pixels[4 * 320 + 1] == 0xee &&
          pixels[4 * 320 + 2] == 0xee && pixels[4 * 320 + 3] == 0xe1);
    /* a, not a capital: row 0 blank, row 3 .....##. */
    CHECK(pixels[4] == 0x11 && pixels[5] == 0x11 && pixels[6] == 0x11 && pixels[7] == 0x11);
    CHECK(pixels[3 * 320 + 4] == 0x11 && pixels[3 * 320 + 5] == 0x11 &&
          pixels[3 * 320 + 6] == 0x1e && pixels[3 * 320 + 7] == 0xe1);
    /* 7f, Ctrl-Backspace's, the house: its roof's top ...##... on row 1. */
    CHECK(pixels[8] == 0x11 && pixels[320 + 8] == 0x11 && pixels[320 + 9] == 0x1e);
    /* c4, a byte past 7f, the box's line: rows 3 and 4 across the cell. */
    CHECK(pixels[12] == 0x11 && pixels[2 * 320 + 15] == 0x11);
    CHECK(pixels[3 * 320 + 12] == 0xee && pixels[4 * 320 + 15] == 0xee);
    CHECK(pixels[6 * 320 + 16] == 0x77);     /* the cursor at 4,0, in its cell's 07 */
    CHECK(pixels[5 * 320 + 16] == 0x00);
    /* Bit 7, blinking, which TextAttr never has, is not a bright
     * background. */
    crt.attrs[0][0] = 0x9e;
    cok_crt_render(&crt, pixels);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x1e && pixels[320] == 0x11);
}

/* The text screen's font: unscii's 8x8 for code page 437 as
 * tools/pcfont.py prints it, every character its own but the blanks. */
static void test_font(void)
{
    uint8_t all[256 * 8];
    for (int c = 0; c < 256; ++c) memcpy(all + c * 8, cok_crt_glyph((uint8_t)c), 8);
    CHECK(fnv(all, sizeof all) == 0x55b2ed49u);
    static const uint8_t A[8] = {0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00};
    static const uint8_t a[8] = {0x00, 0x00, 0x3c, 0x06, 0x3e, 0x66, 0x3e, 0x00};
    static const uint8_t house[8] = {0x00, 0x18, 0x3c, 0x66, 0x66, 0x66, 0x7e, 0x00};
    CHECK(memcmp(cok_crt_glyph('A'), A, 8) == 0 && memcmp(cok_crt_glyph('a'), a, 8) == 0);
    CHECK(memcmp(cok_crt_glyph(0x7f), house, 8) == 0);
    static const uint8_t blank[8] = {0};
    for (int c = 0; c < 256; ++c) {
        bool empty = c == 0 || c == ' ' || c == 0xff;
        CHECK((memcmp(cok_crt_glyph((uint8_t)c), blank, 8) == 0) == empty);
        for (int d = 0; d < c && !empty; ++d)
            CHECK(memcmp(cok_crt_glyph((uint8_t)c), cok_crt_glyph((uint8_t)d), 8) != 0);
    }
    /* Box drawing reaches the cell's edges: c4's two rows across it, b3's
     * column down it; db is the whole cell. */
    const uint8_t *h = cok_crt_glyph(0xc4), *v = cok_crt_glyph(0xb3), *full = cok_crt_glyph(0xdb);
    CHECK(h[3] == 0xff && h[4] == 0xff && h[0] == 0 && h[7] == 0);
    for (int row = 0; row < 8; ++row) CHECK(v[row] == 0x18 && full[row] == 0xff);
}

/* A second ReadLn: Ctrl-F recalls the first line's characters the buffer
 * still holds, up to its CR. */
static const char *typed;

static int type(void *context)
{
    (void)context;
    return *typed == '\0' ? -1 : (unsigned char)*typed++;
}

static void test_lines(void)
{
    static cok_crt crt;
    crt.log = NULL;
    cok_crt_init(&crt);
    cok_keyboard keys = {type, NULL, NULL, NULL};
    char line[81];
    typed = "abc\r";
    CHECK(cok_crt_read_line(&crt, &keys, line, 80) && strcmp(line, "abc") == 0);
    CHECK(crt.line_end == 5 && crt.y == 1 && crt.x == 0);
    typed = "x\x06\x04\r";
    CHECK(cok_crt_read_line(&crt, &keys, line, 80) && strcmp(line, "xbc") == 0);
    CHECK(crt.y == 2 && memcmp(crt.chars[1], "xbc ", 4) == 0); /* the CR not echoed */
    typed = "\x06\r";
    CHECK(cok_crt_read_line(&crt, &keys, line, 2) && strcmp(line, "xb") == 0);
    typed = "q";
    CHECK(!cok_crt_read_line(&crt, &keys, line, 80) && strcmp(line, "q") == 0);
}

/* Keys with COK_KEY_TIMEOUT among them, which a script's \\t gives: no
 * key, so the reads wait on. */
static const int *timed;

static int timed_read(void *context)
{
    (void)context;
    return *timed == -1 ? -1 : *timed++;
}

static void test_timeouts_and_limits(void)
{
    static cok_crt crt;
    crt.log = NULL;
    cok_crt_init(&crt);
    static const int keys[] = {-2, '1', -2, '1', '1', 'c', -2, ':', 0, -2, '\\', 'x', 13, -1};
    timed = keys;
    cok_keyboard keyboard = {timed_read, NULL, NULL, NULL};
    uint8_t file[COK_SETUP_FILE_MAX];
    size_t size;
    CHECK(cok_setup_ask(&crt, &keyboard, NULL, &cok_rules_champions,
                        cok_rules_champions.files.install_dir, file, &size));
    CHECK(memcmp(file, "C\r\nC\r\nJ\r\nC:\\X\\\r\nF\r\n", 18) == 0);
    /* 126 characters at most; the string keeps max of them. */
    cok_crt_init(&crt);
    static int many[140];
    for (int i = 0; i < 130; ++i) many[i] = 'a' + i % 26;
    many[130] = 13;
    many[131] = -1;
    timed = many;
    char line[200];
    CHECK(cok_crt_read_line(&crt, &keyboard, line, 199));
    CHECK(strlen(line) == 126 && crt.line_end == 128 && crt.y == 2);
    CHECK(crt.chars[1][45] == 'a' + 125 % 26 && crt.chars[1][46] == ' ');
    /* The |, unbroken, light grey on black. */
    cok_crt_init(&crt);
    cok_crt_write_string(&crt, "|");
    static uint8_t pixels[COK_CRT_WIDTH * COK_CRT_HEIGHT / 2];
    cok_crt_render(&crt, pixels);
    CHECK(pixels[0] == 0x00 && pixels[1] == 0x07 && pixels[2] == 0x70 && pixels[3] == 0x00);
    CHECK(pixels[3 * 320 + 1] == 0x07 && pixels[7 * 320 + 1] == 0x00);
}

static void test_startup(void)
{
    char dir[512];
    CHECK(test_temp_dir(dir, sizeof dir, "cok-setup") != NULL);
    char path[600], write_path[600];
    char assets[600], saves[600];
    snprintf(assets, sizeof assets, "%s/assets", dir);
    snprintf(saves, sizeof saves, "%s/saves", dir);
    CHECK(mkdir(assets, 0755) == 0 && mkdir(saves, 0755) == 0);
    /* None anywhere: the setup writes SAVES/KRYNN.CFG. */
    CHECK(!cok_config_find(&cok_rules_champions, NULL, saves, assets, path, write_path,
                           sizeof path));
    char expected[700];
    snprintf(expected, sizeof expected, "%s/KRYNN.CFG", saves);
    CHECK(strcmp(path, expected) == 0 && strcmp(write_path, expected) == 0);
    static cok_crt crt;
    cok_crt_init(&crt);
    cok_keyboard keys = {type, NULL, NULL, NULL};
    cok_config config = {0};
    char error[300];
    typed = "362q:\\x\r";
    CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, path, false, &config, error,
                            sizeof error) ==
          COK_SETUP_OK);
    CHECK(config.display == COK_DISPLAY_TANDY && config.sound == COK_SOUND_NONE && config.mouse);
    CHECK(strcmp(config.save_path, "C:\\COK\\SAVE\\") == 0); /* Q is past P */
    CHECK(memcmp(crt.chars[0], "TANDY 1000 USERS", 16) == 0);
    FILE *f = fopen(expected, "rb");
    CHECK(f != NULL);
    char bytes[100] = {0};
    size_t n = fread(bytes, 1, sizeof bytes - 1, f);
    fclose(f);
    CHECK(n == 26 + 14 * 3 && memcmp(bytes, "T\r\nS\r\nM\r\nC:\\COK\\SAVE\\\r\nF\r\n0\r\n", 29) == 0);
    /* Found in SAVES first; the assets' after it. */
    CHECK(cok_config_find(&cok_rules_champions, NULL, saves, assets, path, write_path,
                          sizeof path));
    CHECK(strcmp(path, expected) == 0 && strcmp(write_path, expected) == 0);
    char in_assets[700];
    snprintf(in_assets, sizeof in_assets, "%s/krynn.cfg", assets);
    f = fopen(in_assets, "wb");
    CHECK(f != NULL);
    fputs("C\r\nP\r\nM\r\nA:\\\r\nF\r\n", f);
    fclose(f);
    CHECK(cok_config_find(&cok_rules_champions, NULL, saves, assets, path, write_path,
                          sizeof path));
    CHECK(strcmp(path, expected) == 0);
    CHECK(unlink(expected) == 0);
    CHECK(cok_config_find(&cok_rules_champions, NULL, saves, assets, path, write_path,
                          sizeof path));
    CHECK(strcmp(path, in_assets) == 0 && strcmp(write_path, expected) == 0);
    CHECK(cok_config_find(&cok_rules_champions, NULL, NULL, assets, path, write_path, sizeof path));
    CHECK(strcmp(path, in_assets) == 0 && write_path[0] == '\0');
    /* Read from the assets: only the note. */
    cok_crt_init(&crt);
    typed = "";
    CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, in_assets, true, &config,
                            error, sizeof error) ==
          COK_SETUP_OK);
    CHECK(config.display == COK_DISPLAY_CGA && config.sound == COK_SOUND_SPEAKER);
    /* Initialize Mouse/Joystick's rewrite: into SAVES, line 3 N. */
    CHECK(cok_config_rewrite(in_assets, expected, 'N', NULL, NULL));
    f = fopen(expected, "rb");
    CHECK(f != NULL);
    n = fread(bytes, 1, sizeof bytes - 1, f);
    fclose(f);
    CHECK(n == 17 + 14 * 3 && memcmp(bytes, "C\r\nP\r\nN\r\nA:\\\r\nF\r\n0\r\n", 20) == 0);
    unlink(expected);
    char nowhere[700];
    snprintf(nowhere, sizeof nowhere, "%s/none", dir);
    /* No file to read: Reset's error stops every Write, the file is left
     * empty. */
    CHECK(cok_config_rewrite(nowhere, expected, 'N', NULL, NULL));
    f = fopen(expected, "rb");
    CHECK(f != NULL);
    n = fread(bytes, 1, sizeof bytes - 1, f);
    fclose(f);
    CHECK(n == 0);
    unlink(expected);
    /* Line 3 is the letter alone; lines 1, 2, 4 and 5 are string[255]s. */
    f = fopen(in_assets, "wb");
    CHECK(f != NULL);
    fputs("EGA\r\nT\r\nMOUSE\r\n", f);
    for (int i = 0; i < 300; ++i) fputc('p', f);
    fputs("\r\nF\r\n9\r\n", f);
    fclose(f);
    CHECK(cok_config_rewrite(in_assets, expected, 'N', NULL, NULL));
    static char big[1000];
    f = fopen(expected, "rb");
    CHECK(f != NULL);
    n = fread(big, 1, sizeof big - 1, f);
    fclose(f);
    CHECK(n == 5 + 3 + 3 + 255 + 2 + 3 + 14 * 3 && memcmp(big, "EGA\r\nT\r\nN\r\npp", 13) == 0 &&
          memcmp(big + 11 + 255, "\r\nF\r\n0\r\n", 8) == 0);
    unlink(expected);
    /* --config FILE: read there, written there. */
    CHECK(!cok_config_find(&cok_rules_champions, nowhere, saves, assets, path, write_path,
                           sizeof path));
    CHECK(strcmp(path, nowhere) == 0 && strcmp(write_path, nowhere) == 0);
    /* Input ends in the setup: no file left. */
    cok_crt_init(&crt);
    typed = "36";
    CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, expected, false, &config,
                            error, sizeof error) ==
          COK_SETUP_ENDED);
    CHECK(access(expected, F_OK) != 0);
    /* It cannot be made: the original's message. */
    char unmade[700];
    snprintf(unmade, sizeof unmade, "%s/no/KRYNN.CFG", dir);
    cok_crt_init(&crt);
    typed = "";
    CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, unmade, false, &config,
                            error, sizeof error) ==
          COK_SETUP_UNWRITABLE);
    CHECK(memcmp(crt.chars[0], "Sorry, disk A can't be write-protected.", 39) == 0);
    CHECK(strstr(error, unmade) != NULL);
    CHECK(memcmp(crt.chars[4], "The port stops here: ", 21) == 0);
    /* A device that takes no bytes: the write fails, and what was there is
     * not removed. */
    if (access("/dev/full", W_OK) == 0) {
        cok_crt_init(&crt);
        typed = "111\r";
        CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, "/dev/full", false,
                                &config, error,
                                sizeof error) == COK_SETUP_UNWRITABLE);
        CHECK(access("/dev/full", F_OK) == 0);
        CHECK(memcmp(crt.chars[3], "The port stops here: /dev/full", 30) == 0);
    }
    /* Something there before that is not a file (a link to a device):
     * the setup writes through it, but never removes it, whether input
     * ends or the write fails. */
    char link[700];
    snprintf(link, sizeof link, "%s/null.cfg", dir);
    CHECK(symlink("/dev/null", link) == 0);
    CHECK(!cok_config_find(&cok_rules_champions, link, NULL, NULL, path, write_path, sizeof path));
    cok_crt_init(&crt);
    typed = "1";
    CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, link, false, &config, error,
                            sizeof error) ==
          COK_SETUP_ENDED);
    struct stat st;
    CHECK(lstat(link, &st) == 0);
    unlink(link);
    if (access("/dev/full", W_OK) == 0) {
        CHECK(symlink("/dev/full", link) == 0);
        cok_crt_init(&crt);
        typed = "111\r";
        CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, link, false, &config,
                                error, sizeof error) ==
              COK_SETUP_UNWRITABLE);
        CHECK(lstat(link, &st) == 0);
        unlink(link);
    }
    /* A directory named KRYNN.CFG is missing to FindFirst: the setup runs
     * and cannot make the file. */
    char as_dir[700];
    snprintf(as_dir, sizeof as_dir, "%s/KRYNN.CFG", saves);
    CHECK(mkdir(as_dir, 0755) == 0);
    CHECK(!cok_config_find(&cok_rules_champions, NULL, saves, NULL, path, write_path, sizeof path));
    CHECK(strcmp(path, as_dir) == 0);
    cok_crt_init(&crt);
    CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, path, false, &config, error,
                            sizeof error) ==
          COK_SETUP_UNWRITABLE);
    CHECK(rmdir(as_dir) == 0);
    /* A file the port cannot read is found, never made again: it stops. */
    if (geteuid() != 0) {
        f = fopen(as_dir, "wb");
        CHECK(f != NULL);
        fputs("E\r\nT\r\n", f);
        fclose(f);
        CHECK(chmod(as_dir, 0200) == 0);
        CHECK(cok_config_find(&cok_rules_champions, NULL, saves, NULL, path, write_path,
                              sizeof path));
        cok_crt_init(&crt);
        CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, path, true, &config,
                                error, sizeof error) ==
              COK_SETUP_UNREADABLE);
        CHECK(memcmp(crt.chars[1], "The port stops here: ", 21) == 0);
        chmod(as_dir, 0600);
        f = fopen(as_dir, "rb");
        CHECK(f != NULL && fgetc(f) == 'E');
        fclose(f);
        unlink(as_dir);
    }
    /* --saves naming the assets directory: never written there. */
    char same[700];
    snprintf(same, sizeof same, "%s/assets/.", dir);
    CHECK(cok_config_find(&cok_rules_champions, NULL, same, assets, path, write_path, sizeof path));
    CHECK(strstr(path, "krynn.cfg") != NULL && write_path[0] == '\0');
    CHECK(cok_config_find(&cok_rules_champions, NULL, saves, assets, path, write_path,
                          sizeof path));
    CHECK(write_path[0] != '\0');
    /* Written nowhere: the setup's file read back all the same. */
    cok_crt_init(&crt);
    typed = "111\r";
    CHECK(cok_setup_startup(&crt, &keys, NULL, &cok_rules_champions, "", false, &config, error,
                            sizeof error) ==
          COK_SETUP_OK);
    CHECK(config.display == COK_DISPLAY_CGA && config.sound == COK_SOUND_CMS && !config.joystick);
    unlink(in_assets);
    rmdir(assets);
    rmdir(saves);
    rmdir(dir);
}

int main(void)
{
    test_cases();
    test_parse();
    test_default_path();
    test_crt();
    test_font();
    test_lines();
    test_timeouts_and_limits();
    test_startup();
    puts("setup: ok");
    return 0;
}
