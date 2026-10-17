#define _POSIX_C_SOURCE 200809L /* mkdtemp */

/* The mouse and the joystick (src/input.c, src/devices.c and their users):
 * a menu's click (67b5:03e2), 1743:0167, the joystick's unit and its
 * calibrations against what the original gave in an 8086 emulator
 * (tests/test_input_data.h); then the driver's cursor, the mouse unit,
 * the keys they give, the cursor's hiding around text, a list's clicks,
 * Initialize Mouse/Joystick's file and the setup's calibration. */
#include "adventure.h"
#include "config.h"
#include "input.h"
#include "menu.h"
#include "setup.h"
#include "start.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

#include "test_input_data.h"
#include "test_temp.h"

static cok_adventure game;

/* Numbers from a case's line, after skip words. */
static size_t numbers(const char *line, size_t skip, long *v, size_t max)
{
    const char *p = line;
    for (size_t i = 0; i < skip; ++i) {
        while (*p == ' ') ++p;
        while (*p != ' ' && *p != '\0') ++p;
    }
    size_t count = 0;
    while (count < max) {
        char *end;
        long x = strtol(p, &end, 10);
        if (end == p) break;
        v[count++] = x;
        p = end;
    }
    return count;
}

static void unhex(const char *h, size_t n, char *out)
{
    for (size_t i = 0; i < n / 2; ++i) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (char)v;
    }
    out[n / 2] = '\0';
}

/* A click at x, y, then Escape. */
typedef struct {
    int step, x, y;
} click_script;

static int read_click(void *context)
{
    click_script *s = context;
    game.pointer.clicked = s->step == 0;
    if (s->step++ == 0) {
        game.pointer.x = (int16_t)s->x;
        game.pointer.y = (int16_t)s->y;
        return 13;
    }
    return 27;
}

/* The state a case sets, then cok_menu_read with its click; the key, the
 * extended flag, the selection, Modify's field, the selected character and
 * the pick. */
static void menu_case(const char *line, char *out, size_t size)
{
    char prompt[100] = "", items[100] = "";
    const char *p = line + 5;
    size_t n = strcspn(p, " ");
    if (!(n == 1 && p[0] == '-')) unhex(p, n, prompt);
    p += n + 1;
    n = strcspn(p, " ");
    unhex(p, n, items);
    long v[200];
    numbers(line, 3, v, 200);
    size_t k = 0;
#define NEXT (v[k++])
    int selected = (int)NEXT, x = (int)NEXT, y = (int)NEXT;
    game.mouse.party_clicks = NEXT != 0;
    game.vm.mode = (uint8_t)NEXT;
    game.vm.mem4b00[0x138] = (uint16_t)NEXT;
    game.vm.mem4b00[0xe6] = (uint16_t)NEXT;
    game.vm.mem4b00[0xc3] = (uint16_t)NEXT;
    game.vm.mem4b00[0xc4] = (uint16_t)NEXT;
    game.moving = NEXT != 0;
    size_t members = (size_t)NEXT;
    static cok_character people[8];
    game.party.count = 0;
    for (size_t i = 0; i < members; ++i) game.party.members[game.party.count++] = &people[i];
    long sel = NEXT;
    game.vm.character = sel < 0 ? NULL : people[sel].record;
    game.pick_into = NEXT != 0;
    long pick = NEXT;
    game.pick = pick < 0 ? NULL : people[pick].record;
    game.party_left = (uint8_t)NEXT;
    game.party_right = (uint8_t)NEXT;
    game.party_top = (uint8_t)NEXT;
    game.party_bottom = (uint8_t)NEXT;
    game.aiming = NEXT != 0;
    game.combat_moving = NEXT != 0;
    game.aim_x = (int8_t)NEXT;
    game.aim_y = (int8_t)NEXT;
    game.combat.view_x = (int8_t)NEXT;
    game.combat.view_y = (int8_t)NEXT;
    size_t combatants = (size_t)NEXT;
    memset(game.combat.combatant, 0, sizeof game.combat.combatant);
    game.combat.count = (uint8_t)combatants;
    for (size_t i = 1; i <= combatants; ++i) {
        game.combat.combatant[i].x = (int8_t)NEXT;
        game.combat.combatant[i].y = (int8_t)NEXT;
        long who = NEXT;
        game.combat.combatant[i].character = who < 0 ? NULL : &people[who];
    }
    game.modifying = NEXT != 0;
    game.modify_field = (uint8_t)NEXT;
    game.pointer.list_left = (int8_t)NEXT;
    game.pointer.list_right = (int8_t)NEXT;
    size_t rows = (size_t)NEXT;
    memset(game.pointer.list_rows, 0xff, sizeof game.pointer.list_rows);
    for (size_t i = 0; i < rows; ++i) game.pointer.list_rows[i] = (uint8_t)NEXT;
#undef NEXT
    click_script s = {0, x, y};
    cok_keyboard keys = {read_click, &s, NULL, &game.pointer};
    uint8_t chosen = (uint8_t)selected;
    bool special = false;
    int key = cok_menu_read(&game.screen, &game.font, prompt, items, 13, 15, 10, &chosen, &keys,
                            &special);
    int sel_after = -1, pick_after = -1;
    for (size_t i = 0; i < members; ++i) {
        if (game.vm.character == people[i].record) sel_after = (int)i;
        if (game.pick == people[i].record) pick_after = (int)i;
    }
    snprintf(out, size, "%d %d %d %d %d %d", key, special, chosen, game.modify_field, sel_after,
             pick_after);
}

/* A game port of scripted reads, then the rest state for ever. */
typedef struct {
    long reads[256][4];
    size_t count, next;
    long rest[4];
    unsigned polls;
} scripted_port;

static bool port_read(void *context, unsigned stick, uint16_t *x, uint16_t *y, uint8_t *b)
{
    (void)stick;
    scripted_port *s = context;
    const long *r = s->next < s->count ? s->reads[s->next++] : s->rest;
    ++s->polls;
    if (r[0] == 0) return false;
    *x = (uint16_t)r[1];
    *y = (uint16_t)r[2];
    *b = (uint8_t)r[3];
    return true;
}

typedef struct {
    char shown[16];
    size_t count;
    unsigned failed;
} prompts;

static void note_prompt(cok_stick_prompt which, void *context)
{
    prompts *p = context;
    if (p->count + 1 < sizeof p->shown)
        p->shown[p->count++] = which == COK_STICK_UPPER_LEFT    ? 'U'
                               : which == COK_STICK_LOWER_RIGHT ? 'L'
                                                                : 'F';
    if (which == COK_STICK_FAILED) ++p->failed; /* the callers' Delay(2000) */
}

static void stick_case(const char *line, char *out, size_t size)
{
    char op = line[6];
    long v[1200];
    numbers(line, 2, v, 1200);
    static scripted_port s;
    cok_stick stick = {.on = v[0] != 0};
    for (size_t i = 0; i < 7; ++i) {
        stick.x[i] = (uint16_t)v[1 + i];
        stick.y[i] = (uint16_t)v[8 + i];
    }
    s.count = (size_t)v[15];
    CHECK(s.count <= 256);
    s.next = 0;
    s.polls = 0;
    for (size_t i = 0; i < s.count; ++i)
        for (size_t k = 0; k < 4; ++k) s.reads[i][k] = v[16 + 4 * i + k];
    for (size_t k = 0; k < 4; ++k) s.rest[k] = v[16 + 4 * s.count + k];
    cok_game_port port = {port_read, NULL, &s};
    prompts p = {"", 0, 0};
    char result[32] = "";
    if (op == 'd') {
        snprintf(result, sizeof result, "%u", cok_stick_direction(&stick, &port));
    } else if (op == '1' || op == '2') {
        snprintf(result, sizeof result, "%d", cok_stick_button(&stick, &port, (unsigned)(op - '0')));
    } else if (op == 'p') {
        uint8_t d;
        uint16_t b;
        cok_stick_poll(&stick, &port, &d, &b);
        snprintf(result, sizeof result, "%u %u", d, b);
    } else if (op == 'i') {
        cok_stick_init(&stick, &port);
    } else if (op == 'c') {
        cok_stick_calibrate(&stick, &port, note_prompt, &p, NULL);
    }
    int at = snprintf(out, size, "%s%s | %d", p.shown, result, stick.on);
    for (size_t i = 0; i < 7; ++i) at += snprintf(out + at, size - (size_t)at, " %u", stick.x[i]);
    for (size_t i = 0; i < 7; ++i) at += snprintf(out + at, size - (size_t)at, " %u", stick.y[i]);
    snprintf(out + at, size - (size_t)at, " | %u | %u", s.polls, p.failed);
}

static void against_the_original(void)
{
    char out[256];
    CHECK(cok_adventure_open(&game, &cok_rules_champions, "COKAssets", NULL, NULL));
    for (size_t i = 0; i < sizeof menu_cases / sizeof *menu_cases; ++i) {
        menu_case(menu_cases[i].in, out, sizeof out);
        if (strcmp(out, menu_cases[i].out) != 0) {
            fprintf(stderr, "menu case %zu: %s\n  want %s\n  got  %s\n", i, menu_cases[i].in,
                    menu_cases[i].out, out);
            exit(1);
        }
    }
    for (int dy = -30; dy <= 30; ++dy)
        for (int dx = -30; dx <= 30; ++dx)
            CHECK(cok_mouse_direction(0, 0, (int16_t)dx, (int16_t)dy) ==
                  direction_grid[dy + 30][dx + 30]);
    /* The same from elsewhere: only the differences count. */
    CHECK(cok_mouse_direction(10, 5, 10, 5) == 0x48);
    CHECK(cok_mouse_direction(10, 5, 14, 1) == 0x49);
    CHECK(cok_mouse_direction(10, 5, 20, 6) == 0x4d);
    for (size_t i = 0; i < sizeof stick_cases / sizeof *stick_cases; ++i) {
        stick_case(stick_cases[i].in, out, sizeof out);
        if (strcmp(out, stick_cases[i].out) != 0) {
            fprintf(stderr, "stick case %zu: %.80s...\n  want %s\n  got  %s\n", i,
                    stick_cases[i].in, stick_cases[i].out, out);
            exit(1);
        }
    }
    game.party.count = 0;
    cok_adventure_close(&game);
}

static uint8_t pixel(const uint8_t *pixels, int x, int y)
{
    uint8_t b = pixels[y * 160 + x / 2];
    return (uint8_t)(x % 2 == 0 ? b >> 4 : b & 15);
}

static void cursor(void)
{
    static uint8_t pixels[32000];
    cok_mouse_driver d = {.installed = true, .x = 20, .y = 10, .counter = 0,
                          .masks = cok_mouse_arrow};
    memset(pixels, 0x55, sizeof pixels); /* colour 5 */
    cok_mouse_draw(&d, pixels, false);
    /* At pixel (10, 10): row 0 is the tip, black (screen mask 0, cursor
     * 0), then the screen through; row 1 white (cursor 1) after black. */
    CHECK(pixel(pixels, 10, 10) == 0);
    CHECK(pixel(pixels, 12, 10) == 5);
    CHECK(pixel(pixels, 10, 11) == 0 && pixel(pixels, 11, 11) == 15);
    CHECK(pixel(pixels, 9, 11) == 5 && pixel(pixels, 26, 11) == 5);
    CHECK(pixel(pixels, 10, 9) == 5);
    /* Row 12: screen mask 0x10ff, cursor 0x0600: pixel 3 kept, 5 and 6
     * XORed white on black. */
    CHECK(pixel(pixels, 13, 22) == 5 && pixel(pixels, 15, 22) == 15 && pixel(pixels, 14, 22) == 0);
    /* Hidden, or no driver: nothing. */
    memset(pixels, 0x55, sizeof pixels);
    d.counter = -1;
    cok_mouse_draw(&d, pixels, false);
    d.counter = 0;
    d.installed = false;
    cok_mouse_draw(&d, pixels, false);
    for (size_t i = 0; i < sizeof pixels; ++i) CHECK(pixels[i] == 0x55);
    /* At the bottom right corner: clipped, nothing past the screen nor
     * wrapped to the next row's start. */
    d.installed = true;
    d.x = 639;
    d.y = 199;
    enum { PAST = 16 * 160 };
    uint8_t *exact = malloc(32000 + PAST);
    CHECK(exact != NULL);
    memset(exact, 0x55, 32000 + PAST);
    cok_mouse_draw(&d, exact, false);
    CHECK(pixel(exact, 319, 199) == 0 && pixel(exact, 0, 199) == 5);
    for (size_t i = 32000; i < 32000 + PAST; ++i) CHECK(exact[i] == 0x55);
    memset(exact, 0x55, 32000);
    d.y = 100;
    cok_mouse_draw(&d, exact, false);
    for (int y = 100; y < 117; ++y)
        for (int x = 0; x < 16; ++x) CHECK(pixel(exact, x, y) == 5);
    free(exact);
    memset(pixels, 0x55, sizeof pixels);
    /* CGA: two bits a pixel, eight across; colours AND and XOR. */
    memset(pixels, 0x22, sizeof pixels); /* colour 2 */
    d = (cok_mouse_driver){.installed = true, .x = 0, .y = 0, .counter = 0,
                           .masks = cok_mouse_arrow_cga};
    cok_mouse_draw(&d, pixels, true);
    CHECK(pixel(pixels, 0, 0) == 0 && pixel(pixels, 1, 0) == 0 && pixel(pixels, 2, 0) == 2);
    CHECK(pixel(pixels, 0, 1) == 0 && pixel(pixels, 1, 1) == 3 && pixel(pixels, 5, 1) == 2);
    CHECK(pixel(pixels, 8, 1) == 2);
    /* Show and hide: the counter stops at 0 going up, not going down. */
    cok_mouse_driver_hide(&d);
    cok_mouse_driver_hide(&d);
    cok_mouse_driver_show(&d);
    CHECK(d.counter == -1);
    cok_mouse_driver_show(&d);
    cok_mouse_driver_show(&d);
    CHECK(d.counter == 0);
}

static void mouse_unit(void)
{
    cok_mouse m = {0};
    /* No driver: off, but clicks on the party list on (3e99:008f). */
    cok_mouse_start(&m, false);
    CHECK(!m.on && m.party_clicks);
    cok_mouse_show(&m);
    CHECK(m.driver.counter == 0); /* nothing called */
    m = (cok_mouse){.driver = {.installed = true, .x = 7, .y = 9}};
    cok_mouse_start(&m, true);
    CHECK(m.on && !m.narrow && m.button_count == 2);
    CHECK(m.driver.counter == 0 && m.driver.masks == cok_mouse_arrow_cga);
    CHECK(m.driver.x == 639 && m.driver.y == 100);
    unsigned b = 9;
    int16_t x = 0, y = 0;
    m.driver.buttons = 2;
    cok_mouse_get(&m, &b, &x, &y);
    CHECK(b == 2 && x == 319 && y == 100);
    cok_mouse_set(&m, 160, 100);
    CHECK(m.driver.x == 320 && m.driver.moved);
    cok_mouse_set(&m, 400, 250); /* kept on the screen */
    CHECK(m.driver.x == 639 && m.driver.y == 199);
    m.on = false;
    b = 9;
    cok_mouse_get(&m, &b, &x, &y);
    CHECK(b == 9);
}

/* The adventure's keys with the devices on. */
static int device_keys[8], device_count, device_at;

static int read_device(void *context)
{
    (void)context;
    return device_at < device_count ? device_keys[device_at++] : -1;
}

static bool no_key(cok_adventure *g, void *context)
{
    (void)g;
    (void)context;
    return false;
}

static scripted_port stick_port;

static void keys(void)
{
    cok_keyboard k = {read_device, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {.key_pending = no_key};
    CHECK(cok_adventure_open(&game, &cok_rules_champions, "COKAssets", &k, &hooks));
    game.mouse.driver.installed = true;
    uint16_t zero[7] = {0};
    cok_adventure_devices(&game, true, false, zero, zero);
    cok_adventure_mouse_startup(&game);
    CHECK(game.mouse.on && game.pointer.x == 0xa0 && game.pointer.y == 100);
    CHECK(game.mouse.driver.x == 320);
    /* A click: Enter at its place; the right button Escape; both 0. */
    game.mouse.driver.x = 101;
    game.mouse.driver.y = 77;
    game.mouse.driver.buttons = 1;
    device_keys[0] = COK_KEY_DEVICE;
    device_count = 1;
    device_at = 0;
    cok_keyboard keyboard = cok_adventure_keyboard(&game);
    CHECK(keyboard.read(keyboard.context) == 13);
    CHECK(game.pointer.clicked && game.pointer.x == 50 && game.pointer.y == 77);
    CHECK(game.device_wait == COK_WAIT_KEYS);
    game.mouse.driver.buttons = 2;
    device_at = 0;
    CHECK(keyboard.read(keyboard.context) == 27);
    game.mouse.driver.buttons = 3;
    device_at = 0;
    CHECK(keyboard.read(keyboard.context) == 0);
    /* Up again by the time it is read: the read goes on to the key. */
    game.mouse.driver.buttons = 0;
    device_keys[0] = COK_KEY_DEVICE;
    device_keys[1] = 'q';
    device_count = 2;
    device_at = 0;
    CHECK(keyboard.read(keyboard.context) == 'q' && !game.pointer.clicked);
    device_count = 1;
    /* A key is no click. */
    device_keys[0] = 'a';
    device_at = 0;
    CHECK(keyboard.read(keyboard.context) == 'a' && !game.pointer.clicked);
    /* The demonstration waits for a key with the mouse on. */
    game.mouse.driver.buttons = 0;
    game.demo = true;
    device_at = 0;
    CHECK(cok_adventure_wait_key(&game) == 'a');
    game.demo = false;
    /* KeyPressed: a button down. */
    game.mouse.driver.buttons = 0;
    CHECK(!cok_adventure_key_pending(&game));
    game.mouse.driver.buttons = 4;
    CHECK(cok_adventure_key_pending(&game));
    /* Ready only while the game waits on it. */
    CHECK(!cok_adventure_device_ready(&game));
    game.device_wait = COK_WAIT_MOUSE;
    CHECK(cok_adventure_device_ready(&game));
    game.device_wait = COK_WAIT_KEYS;
    /* 546c:3334 after a click: the selection as the click left it. */
    static cok_character a, b;
    game.party.members[0] = &a;
    game.party.members[1] = &b;
    game.party.count = 2;
    game.vm.character = b.record;
    game.pointer.clicked = true;
    CHECK(cok_adventure_special(&game, 0x48) == b.record);
    game.pointer.clicked = false;
    CHECK(cok_adventure_special(&game, 0x48) == a.record);
    game.party.count = 0;
    game.vm.character = NULL;

    /* The joystick: a push its keypad digit, else button 1 Enter, 2
     * Escape. */
    game.mouse.on = false;
    game.port = (cok_game_port){port_read, NULL, &stick_port};
    stick_port = (scripted_port){.count = 0, .rest = {1, 129, 129, 0}};
    uint16_t x[7] = {1, 40, 80, 120, 160, 200, 250}, y[7] = {1, 40, 80, 120, 160, 200, 250};
    cok_adventure_devices(&game, false, true, x, y);
    CHECK(game.stick.on && game.stick.x[3] == 120);
    CHECK(!cok_adventure_key_pending(&game));
    stick_port.rest[1] = 1; /* pushed left */
    CHECK(cok_adventure_key_pending(&game));
    CHECK(cok_adventure_device_key(&game) == '4');
    stick_port.rest[1] = 129;
    stick_port.rest[3] = 1;
    CHECK(cok_adventure_device_key(&game) == 13);
    CHECK(cok_adventure_key_pending(&game)); /* button 1 alone */
    stick_port.rest[3] = 3; /* button 2 first */
    CHECK(cok_adventure_device_key(&game) == 27);
    stick_port.rest[3] = 0;
    CHECK(cok_adventure_device_key(&game) == -1);
    /* No stick found: off, thresholds cleared. */
    stick_port.rest[0] = 0;
    cok_adventure_devices(&game, false, true, x, y);
    CHECK(!game.stick.on && game.stick.x[3] == 0);
    cok_adventure_close(&game);
}

/* Scripted input through the adventure's keyboard: a key, or the mouse at
 * x, y with buttons (as the front end gives COK_KEY_DEVICE); the game's
 * waits on the buttons let them come up. */
typedef struct {
    int key, x, y;
    unsigned buttons;
} input_step;

static const input_step *steps;
static size_t step_count, step_at;
static unsigned device_waits;
static bool typed_over;

static int read_steps(void *context)
{
    (void)context;
    if (step_at >= step_count) return -1;
    const input_step *s = &steps[step_at++];
    if (s->key != COK_KEY_DEVICE) return s->key;
    game.mouse.driver.x = s->x * 2;
    game.mouse.driver.y = s->y;
    game.mouse.driver.buttons = s->buttons;
    return COK_KEY_DEVICE;
}

static void wait_devices(cok_adventure *g, unsigned ms, void *context)
{
    (void)ms;
    (void)context;
    ++device_waits;
    g->mouse.driver.buttons = 0;
}

static bool typed_pending(cok_adventure *g, void *context)
{
    (void)g;
    (void)context;
    return typed_over && step_at < step_count;
}

static void open_with_steps(const input_step *s, size_t n)
{
    static cok_keyboard k = {read_steps, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {.key_pending = typed_pending, .device_wait = wait_devices};
    CHECK(cok_adventure_open(&game, &cok_rules_champions, "COKAssets", &k, &hooks));
    game.mouse.driver.installed = true;
    uint16_t zero[7] = {0};
    cok_adventure_devices(&game, true, false, zero, zero);
    cok_adventure_mouse_startup(&game);
    steps = s;
    step_count = n;
    step_at = 0;
    device_waits = 0;
    typed_over = false;
}

/* The menus wait for the buttons to come up after a click (67b5:0c2d); a
 * key typed with the click is read in its place (1614:025b); pickers take
 * the party list's clicks into the pick, in the start menu too, and Pick
 * Character does; leaving the start menu by Begin lets the party list take
 * them. */
static void pickers(void)
{
    static cok_character people[3];
    for (size_t i = 0; i < 3; ++i) {
        memset(&people[i], 0, sizeof people[i]);
        people[i].record[0] = 1;
        people[i].record[1] = (uint8_t)('A' + i);
        people[i].record[0x189] = 1;
    }
    /* A click on Select, the buttons held: the menu waits. */
    static const input_step select[] = {{COK_KEY_DEVICE, 4, 196, 1}};
    open_with_steps(select, 1);
    cok_keyboard keys = cok_adventure_keyboard(&game);
    uint8_t chosen = 1;
    bool special;
    CHECK(cok_menu_read(&game.screen, &game.font, "", "Select Exit", 13, 15, 10, &chosen, &keys,
                        &special) == 'S');
    CHECK(device_waits == 1 && game.mouse.driver.buttons == 0);
    /* A key typed with it: the key, the click kept. */
    static const input_step over[] = {{COK_KEY_DEVICE, 4, 196, 1}, {'e', 0, 0, 0}};
    steps = over;
    step_count = 2;
    step_at = 0;
    typed_over = true;
    CHECK(keys.read(keys.context) == 'e' && game.pointer.clicked);
    typed_over = false;
    cok_adventure_close(&game);

    /* The picker (6346:32c7) from Trade, in the start menu (mode 0, the
     * party list's clicks off): the second member clicked into the pick,
     * the selection kept; then Enter. */
    static const input_step pick[] = {{COK_KEY_DEVICE, 20, 5 * 8 + 2, 1}, {13, 0, 0, 0}};
    open_with_steps(pick, 2);
    for (size_t i = 0; i < 3; ++i) game.party.members[game.party.count++] = &people[i];
    game.vm.mode = 0;
    game.vm.character = people[0].record;
    game.mouse.party_clicks = false;
    bool ended;
    uint8_t *who = cok_adventure_pick(&game, "Trade with Whom?", people[0].record, true, &ended);
    CHECK(!ended && who == people[1].record && game.vm.character == people[0].record);
    CHECK(!game.mouse.party_clicks && !game.pick_into);
    /* Pick Character (546c:36cc) likewise, into the selection. */
    static const input_step pick2[] = {{COK_KEY_DEVICE, 20, 6 * 8 + 2, 1}, {'S', 0, 0, 0}};
    steps = pick2;
    step_count = 2;
    step_at = 0;
    CHECK(cok_start_pick(&game) && game.vm.character == people[2].record);
    CHECK(!game.mouse.party_clicks);
    /* Begin (4def:04c3). */
    CHECK(cok_start_command(&game, 'B') && game.mouse.party_clicks);
    game.party.count = 0;
    cok_adventure_close(&game);
}

/* Initialize Mouse/Joystick hides the cursor for good when it turns the
 * mouse off (4def:6072), disables both (6222) or turns the joystick on
 * (6180), with the mouse off after; on again, it shows. */
static void init_hides(void)
{
    static const input_step off[] = {{'S', 0, 0, 0}, {27, 0, 0, 0}};
    open_with_steps(off, 2);
    CHECK(game.mouse.on && game.mouse.driver.counter == 0);
    cok_start_mouse(&game);
    CHECK(!game.mouse.on && game.mouse.driver.counter == -1);
    steps = off;
    step_count = 2;
    step_at = 0;
    cok_start_mouse(&game); /* on again */
    CHECK(game.mouse.on && game.mouse.driver.counter == 0);
    static const input_step both[] = {{0, 0, 0, 0}, {0x50, 0, 0, 0}, {0, 0, 0, 0}, {0x50, 0, 0, 0},
                                      {'S', 0, 0, 0}, {27, 0, 0, 0}};
    steps = both;
    step_count = 6;
    step_at = 0;
    cok_start_mouse(&game);
    CHECK(!game.mouse.on && game.mouse.driver.counter == -1);
    steps = off;
    step_count = 2;
    step_at = 0;
    cok_start_mouse(&game);
    CHECK(game.mouse.on && game.mouse.driver.counter == 0);
    /* The joystick on, calibrated: the mouse off, hidden. */
    scripted_port *s = &stick_port;
    *s = (scripted_port){.count = 0, .rest = {1, 129, 129, 0}};
    long centre[4] = {1, 129, 129, 0}, ul[4] = {1, 10, 12, 1}, lr[4] = {1, 250, 240, 1};
    for (int i = 0; i < 21; ++i) memcpy(s->reads[s->count++], centre, sizeof centre);
    for (int i = 0; i < 21; ++i) memcpy(s->reads[s->count++], ul, sizeof ul);
    memcpy(s->reads[s->count++], centre, sizeof centre);
    for (int i = 0; i < 21; ++i) memcpy(s->reads[s->count++], lr, sizeof lr);
    game.port = (cok_game_port){port_read, NULL, s};
    static const input_step stick[] = {{0, 0, 0, 0}, {0x50, 0, 0, 0}, {'S', 0, 0, 0},
                                       {27, 0, 0, 0}};
    steps = stick;
    step_count = 4;
    step_at = 0;
    cok_start_mouse(&game);
    CHECK(game.stick.on && game.stick.x[0] == 10 && game.stick.y[6] == 240);
    CHECK(!game.mouse.on && game.mouse.driver.counter == -1);
    cok_adventure_close(&game);
}

/* The window's pixels to the driver's coordinates by their centres, and
 * back, at sizes whose scale is not whole: each game pixel's window pixels
 * give it, and the pixel window_axis gives maps back to it. */
static void window_mapping(void)
{
    static const struct { long origin, size; int units, group; } axes[] = {
        {0, 320, 640, 2},   {0, 240, 200, 1},   /* 320x240, 4:3 */
        {69, 1228, 640, 2}, {0, 768, 200, 1},   /* 1366x768, 1:1 */
        {107, 1066, 640, 2}, {0, 800, 200, 1},  /* 1280x800, 4:3 */
        {17, 666, 640, 2},  {0, 500, 200, 1},   /* 700x500, 4:3 */
        {0, 640, 640, 2},   {0, 720, 200, 1},
    };
    for (size_t a = 0; a < sizeof axes / sizeof *axes; ++a) {
        long origin = axes[a].origin, size = axes[a].size;
        int units = axes[a].units, group = axes[a].group, pixels = units / group;
        for (long p = origin; p < origin + size; ++p) {
            /* The game pixel the renderer shows there: where its centre falls. */
            long shown = ((2 * (p - origin) + 1) * pixels) / (2 * size);
            CHECK(cok_pointer_axis(p, origin, size, units) / group == shown);
        }
        for (int v = 0; v < units; v += group) {
            long p = cok_window_axis(v, group, origin, size, units);
            CHECK(cok_pointer_axis(p, origin, size, units) / group == v / group);
        }
    }
    CHECK(cok_pointer_axis(-5, 0, 640, 640) == 0 && cok_pointer_axis(700, 0, 640, 640) == 639);
}

/* The cursor's hiding: running text hides it while it prints (and so
 * while it pages); a window past the screen leaves it hidden; text input
 * drawing past column 39 hides it for good. */
static int typed[64], typed_count, typed_at;

static int read_typed(void *context)
{
    (void)context;
    return typed_at < typed_count ? typed[typed_at++] : -1;
}

static void hiding(void)
{
    cok_keyboard k = {read_typed, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, &cok_rules_champions, "COKAssets", &k, NULL));
    game.mouse.driver.installed = true;
    game.mouse.on = true;
    game.mouse.driver.counter = 0;
    cok_adventure_wrap(&game, &game.vm.cursor, "Hello", (cok_text_window){1, 17, 38, 22}, 10,
                       true, NULL);
    CHECK(game.mouse.driver.counter == 0);
    cok_adventure_wrap(&game, &game.vm.cursor, "Hello", (cok_text_window){1, 25, 38, 26}, 10,
                       true, NULL);
    CHECK(game.mouse.driver.counter == -1);
    /* 1521:04de-04e4's limits: a top up to 0x18, a bottom or right up to
     * 0x27 show it again; a right of 0x28 does not. */
    static const cok_text_window shown[] = {{1, 0x18, 38, 0x18}, {1, 17, 38, 0x19},
                                            {1, 17, 38, 0x27}, {0x27, 17, 0x27, 22}};
    for (size_t i = 0; i < sizeof shown / sizeof *shown; ++i) {
        game.mouse.driver.counter = 0;
        cok_adventure_wrap(&game, &game.vm.cursor, "Hi", shown[i], 10, true, NULL);
        CHECK(game.mouse.driver.counter == 0);
    }
    static const cok_text_window hidden[] = {{1, 17, 0x28, 22}, {1, 17, 38, 0x28},
                                             {0x28, 17, 0x28, 22}};
    for (size_t i = 0; i < sizeof hidden / sizeof *hidden; ++i) {
        game.mouse.driver.counter = 0;
        cok_adventure_wrap(&game, &game.vm.cursor, "Hi", hidden[i], 10, true, NULL);
        CHECK(game.mouse.driver.counter == -1);
    }
    game.mouse.driver.counter = 0;
    /* INPUT STRING's 40 characters: the 40th drawn at column 40, and a
     * Backspace there, each hide it once more. */
    typed_count = 0;
    for (int i = 0; i < 40; ++i) typed[typed_count++] = 'a';
    typed[typed_count++] = 8;
    typed[typed_count++] = 'b';
    typed[typed_count++] = 13;
    typed_at = 0;
    cok_keyboard raw = cok_adventure_keyboard_raw(&game);
    char line[41];
    CHECK(cok_text_input(&game.screen, &game.font, "", 10, 0, 40, &raw, line));
    CHECK(game.mouse.driver.counter == -3);
    CHECK(strlen(line) == 40 && line[39] == 'B');
    /* Shorter lines do not. */
    game.mouse.driver.counter = 0;
    typed_count = 0;
    for (int i = 0; i < 15; ++i) typed[typed_count++] = 'a';
    typed[typed_count++] = 13;
    typed_at = 0;
    CHECK(cok_text_input(&game.screen, &game.font, "Character name: ", 13, 0, 15, &raw, line));
    CHECK(game.mouse.driver.counter == 0);
    cok_adventure_close(&game);
}

/* A list (67b5:1368): a click on a row picks it out, the window's columns
 * taking clicks while the list shows; Select on row 24 takes it. */
static int clicks[8][3], click_count, click_at;

static int read_clicks(void *context)
{
    (void)context;
    if (click_at >= click_count) return -1;
    int *c = clicks[click_at++];
    game.pointer.clicked = c[0] == 13;
    game.pointer.x = (int16_t)c[1];
    game.pointer.y = (int16_t)c[2];
    return c[0];
}

static void list_clicks(void)
{
    cok_keyboard k = {read_clicks, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, &cok_rules_champions, "COKAssets", &k, NULL));
    const char *items[4] = {"Alpha", "Beta", "Gamma", "Delta"};
    int index = 0, top = 0;
    uint8_t selected = 1;
    /* Row 14 (the third row of a list from row 12); row 23, below the
     * rows shown, which the menu gives as Enter and the list passes over;
     * then Select (at column 0: "Select" from the prompt's end). */
    int script[3][3] = {{13, 100, 14 * 8 + 3}, {13, 300, 23 * 8}, {13, 2, 24 * 8 + 1}};
    memcpy(clicks, script, sizeof script);
    click_count = 3;
    click_at = 0;
    cok_keyboard keys = {read_clicks, NULL, NULL, &game.pointer};
    int key = cok_menu_list(&game.screen, &game.font, items, 4, (cok_text_window){9, 12, 0x26, 0x16},
                            15, 10, true, &index, &top, &selected, &keys);
    CHECK(key == 'S' && index == 2);
    CHECK(game.pointer.list_left == 0x7f && game.pointer.list_right == -0x80);
    CHECK(game.pointer.list_rows[0] == 12 && game.pointer.list_rows[3] == 15 &&
          game.pointer.list_rows[4] == 0xff);
    /* Exit on row 24 cancels. */
    int exit_script[1][3] = {{13, 8 * 8 + 4, 24 * 8}};
    memcpy(clicks, exit_script, sizeof exit_script);
    click_count = 1;
    click_at = 0;
    key = cok_menu_list(&game.screen, &game.font, items, 4, (cok_text_window){9, 12, 0x26, 0x16},
                        15, 10, true, &index, &top, &selected, &keys);
    CHECK(key == 0);
    cok_adventure_close(&game);
}

/* Initialize Mouse/Joystick's file: line 3 and the thresholds. */
static void rewrite(void)
{
    char dir[512];
    CHECK(test_temp_dir(dir, sizeof dir, "cok_input") != NULL);
    char from[600], to[600];
    snprintf(from, sizeof from, "%s/from", dir);
    snprintf(to, sizeof to, "%s/to", dir);
    FILE *f = fopen(from, "wb");
    CHECK(f != NULL);
    fputs("E\r\nT\r\nM\r\nC:\\COK\\SAVE\\\r\nF\r\n", f);
    fclose(f);
    uint16_t x[7] = {1, 2, 3, 4, 5, 6, 65535}, y[7] = {7, 8, 9, 10, 11, 12, 13};
    CHECK(cok_config_rewrite(from, to, 'J', x, y));
    char text[256] = "";
    f = fopen(to, "rb");
    CHECK(f != NULL);
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';
    CHECK(strcmp(text, "E\r\nT\r\nJ\r\nC:\\COK\\SAVE\\\r\nF\r\n1\r\n7\r\n2\r\n8\r\n3\r\n9\r\n4\r\n"
                       "10\r\n5\r\n11\r\n6\r\n12\r\n65535\r\n13\r\n") == 0);
    cok_config c = cok_config_parse_stick((const uint8_t *)text, n, true);
    CHECK(c.joystick && !c.mouse && c.joystick_x[6] == 65535 && c.joystick_y[0] == 7);
    c = cok_config_parse((const uint8_t *)text, n); /* no stick: off, no thresholds */
    CHECK(!c.joystick && !c.mouse && c.joystick_x[6] == 0);
    remove(from);
    remove(to);
    rmdir(dir);
}

/* The setup's calibration (177c:029d) on the text screen: [1] JOYSTICK,
 * the stick centred, then pushed up-left and down-right with button 1. */
static const int setup_keys[] = {'2', '4', '1', 'd', ':', '\\', 'x', 13};
static size_t setup_at;

static int read_setup(void *context)
{
    (void)context;
    return setup_at < sizeof setup_keys / sizeof *setup_keys ? setup_keys[setup_at++] : -1;
}

static void setup_calibration(void)
{
    static cok_crt crt;
    cok_crt_init(&crt);
    scripted_port *s = &stick_port;
    *s = (scripted_port){.count = 0};
    long reads[][4] = {{1, 100, 100, 0}}; /* the probe */
    memcpy(s->reads[s->count++], reads[0], sizeof reads[0]);
    for (int i = 0; i < 20; ++i) {
        long r[4] = {1, 100 + i % 2, 98, 0};
        memcpy(s->reads[s->count++], r, sizeof r);
    }
    for (int i = 0; i < 21; ++i) {
        long r[4] = {1, 10 + i % 3, 12, 1};
        memcpy(s->reads[s->count++], r, sizeof r);
    }
    long up[4] = {1, 100, 100, 0};
    memcpy(s->reads[s->count++], up, sizeof up);
    for (int i = 0; i < 21; ++i) {
        long r[4] = {1, 190, 200 - i % 2, 1};
        memcpy(s->reads[s->count++], r, sizeof r);
    }
    long rest[4] = {1, 100, 100, 0};
    memcpy(s->rest, rest, sizeof rest);
    cok_game_port port = {port_read, NULL, s};
    cok_setup_hooks hooks = {NULL, NULL, &port, NULL, false};
    cok_keyboard keys = {read_setup, NULL, NULL, NULL};
    uint8_t file[COK_SETUP_FILE_MAX];
    size_t size = 0;
    setup_at = 0;
    CHECK(cok_setup_ask(&crt, &keys, &hooks, &cok_rules_champions,
                        cok_rules_champions.files.install_dir, file, &size));
    file[size] = '\0';
    /* Centre 100 across (100.5 truncated) and 98 down; the least of the
     * upper left, the most of the lower right; the thirds between. */
    CHECK(strstr((const char *)file, "J\r\nD:\\X\\\r\nF\r\n10\r\n12\r\n40\r\n40\r\n70\r\n69\r\n"
                                     "100\r\n98\r\n130\r\n132\r\n160\r\n166\r\n190\r\n200\r\n") !=
          NULL);
    /* The second screen's line is on the text screen. */
    CHECK(memcmp(crt.chars[0], "Push JOYSTICK #1 to the extreme lower right position and press "
                               "BUTTON #1.", 73) == 0);
    /* --input joystick over a file without J: the same calibration at
     * startup into the config, across and down apart. */
    *s = (scripted_port){.count = 0, .rest = {1, 100, 100, 0}};
    for (int i = 0; i < 21; ++i) {
        long r[4] = {1, 100, 90, 0};
        memcpy(s->reads[s->count++], r, sizeof r);
    }
    for (int i = 0; i < 21; ++i) {
        long r[4] = {1, 10, 20, 1};
        memcpy(s->reads[s->count++], r, sizeof r);
    }
    memcpy(s->reads[s->count++], up, sizeof up);
    for (int i = 0; i < 21; ++i) {
        long r[4] = {1, 190, 170, 1};
        memcpy(s->reads[s->count++], r, sizeof r);
    }
    cok_config config = {0};
    cok_crt_init(&crt);
    CHECK(cok_setup_calibrate(&crt, &hooks, &config) && config.joystick);
    static const uint16_t cx[7] = {10, 40, 70, 100, 130, 160, 190};
    static const uint16_t cy[7] = {20, 43, 66, 90, 116, 143, 170};
    CHECK(memcmp(config.joystick_x, cx, sizeof cx) == 0);
    CHECK(memcmp(config.joystick_y, cy, sizeof cy) == 0);
    /* Without a stick: no screens, zeros. */
    *s = (scripted_port){.rest = {0, 0, 0, 0}};
    setup_at = 0;
    cok_crt_init(&crt);
    CHECK(cok_setup_ask(&crt, &keys, &hooks, &cok_rules_champions,
                        cok_rules_champions.files.install_dir, file, &size));
    file[size] = '\0';
    CHECK(strstr((const char *)file, "F\r\n0\r\n0\r\n0\r\n") != NULL);
}

/* The stick's bands at their edges, every cell of DS:4334, and the
 * calibration's 16-bit sums and thirds. */
static void stick_edges(void)
{
    cok_stick stick = {.on = true, .x = {1, 40, 80, 120, 160, 200, 250},
                       .y = {1, 40, 80, 120, 160, 200, 250}};
    scripted_port *s = &stick_port;
    *s = (scripted_port){.rest = {1, 120, 40, 0}};
    cok_game_port port = {port_read, NULL, s};
    /* A count at a threshold is in the band below it. */
    CHECK(cok_stick_direction(&stick, &port) == 0); /* bands 2, 0: up */
    s->rest[1] = 200;
    s->rest[2] = 129;
    CHECK(cok_stick_direction(&stick, &port) == 8); /* band 4 */
    s->rest[1] = 201;
    CHECK(cok_stick_direction(&stick, &port) == 2); /* band 5: right */
    static const uint8_t table[6][6] = {
        {7, 7, 0, 0, 1, 1}, {7, 8, 8, 8, 8, 1}, {6, 8, 8, 8, 8, 2},
        {6, 8, 8, 8, 8, 2}, {5, 8, 8, 8, 8, 3}, {5, 5, 4, 4, 3, 3},
    };
    static const long in_band[6] = {1, 50, 100, 140, 180, 255};
    for (int by = 0; by < 6; ++by) {
        for (int bx = 0; bx < 6; ++bx) {
            s->rest[1] = in_band[bx];
            s->rest[2] = in_band[by];
            CHECK(cok_stick_direction(&stick, &port) == table[by][bx]);
        }
    }
    /* Counts past 32767: the centre's sum and the thirds wrap at 16 bits
     * (177c:02ec, 03ca, 0507), the lower right's two thirds below its one
     * third. */
    *s = (scripted_port){.count = 0, .rest = {1, 60000, 129, 0}};
    long probe[4] = {1, 40000, 129, 0};
    memcpy(s->reads[s->count++], probe, sizeof probe);
    for (int i = 0; i < 20; ++i) memcpy(s->reads[s->count++], probe, sizeof probe);
    long ul[4] = {1, 10, 129, 1}, up[4] = {1, 40000, 129, 0}, lr[4] = {1, 60000, 129, 1};
    for (int i = 0; i < 21; ++i) memcpy(s->reads[s->count++], ul, sizeof ul);
    memcpy(s->reads[s->count++], up, sizeof up);
    for (int i = 0; i < 21; ++i) memcpy(s->reads[s->count++], lr, sizeof lr);
    prompts p = {"", 0, 0};
    stick.on = true;
    CHECK(cok_stick_calibrate(&stick, &port, note_prompt, &p, NULL));
    static const uint16_t x[7] = {10, 232, 455, 678, 20452, 18380, 60000};
    CHECK(memcmp(stick.x, x, sizeof x) == 0);
    CHECK(strcmp(p.shown, "UL") == 0 && stick.on);
}

int main(void)
{
    against_the_original();
    cursor();
    stick_edges();
    mouse_unit();
    keys();
    pickers();
    init_hides();
    window_mapping();
    hiding();
    list_clicks();
    rewrite();
    setup_calibration();
    puts("input tests passed");
    return 0;
}
