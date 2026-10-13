#include "keymap.h"
#include "keyscript.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* A key, its modifiers and the codes the game reads for it: count 1 for
 * an ASCII code, 2 for 0 and a scan code, 0 for nothing. */
typedef struct {
    const char *what;
    SDL_Keycode key;
    SDL_Scancode scancode;
    Uint16 mod;
    int count, code, scan;
} expect;

#define CH(c, s) (SDL_Keycode)(c), SDL_SCANCODE_##s
#define KEY(s) SDLK_##s, SDL_SCANCODE_##s
#define KP(s) SDLK_KP_##s, SDL_SCANCODE_KP_##s

/* Every key the port compares a read against (README, Playing), and the
 * ones INT 16h function 0 gives besides or drops. */
static const expect table[] = {
    /* Menus, hotkeys and typing: letters (case-insensitive hotkeys, typed
     * as capitals by text input), digits, space, punctuation. */
    {"a", CH('a', A), KMOD_NONE, 1, 'a', 0},
    {"Shift-a", CH('a', A), KMOD_LSHIFT, 1, 'A', 0},
    {"CapsLock a", CH('a', A), KMOD_CAPS, 1, 'A', 0},
    {"CapsLock Shift-a", CH('a', A), KMOD_CAPS | KMOD_RSHIFT, 1, 'a', 0},
    {"z", CH('z', Z), KMOD_NONE, 1, 'z', 0},
    {"Shift-z", CH('z', Z), KMOD_LSHIFT, 1, 'Z', 0},
    {"1", CH('1', 1), KMOD_NONE, 1, '1', 0},
    {"0", CH('0', 0), KMOD_NONE, 1, '0', 0},
    {"Shift-1", CH('1', 1), KMOD_LSHIFT, 1, '!', 0},
    {"Shift-2", CH('2', 2), KMOD_LSHIFT, 1, '@', 0},
    {"Shift-0", CH('0', 0), KMOD_LSHIFT, 1, ')', 0},
    {"space", KEY(SPACE), KMOD_NONE, 1, ' ', 0},
    {"-", CH('-', MINUS), KMOD_NONE, 1, '-', 0},
    {"Shift--", CH('-', MINUS), KMOD_LSHIFT, 1, '_', 0},
    {"\\", CH('\\', BACKSLASH), KMOD_NONE, 1, '\\', 0},
    {"Shift-\\", CH('\\', BACKSLASH), KMOD_LSHIFT, 1, '|', 0},
    {"'", CH('\'', APOSTROPHE), KMOD_NONE, 1, '\'', 0},
    {"Shift-'", CH('\'', APOSTROPHE), KMOD_LSHIFT, 1, '"', 0},
    {"/", CH('/', SLASH), KMOD_NONE, 1, '/', 0},
    {"Shift-/", CH('/', SLASH), KMOD_LSHIFT, 1, '?', 0},
    {"`", CH('`', GRAVE), KMOD_NONE, 1, '`', 0},
    {"Shift-;", CH(';', SEMICOLON), KMOD_LSHIFT, 1, ':', 0},
    {"Shift-.", CH('.', PERIOD), KMOD_LSHIFT, 1, '>', 0},
    /* Enter, Escape, Backspace, Tab. */
    {"Enter", KEY(RETURN), KMOD_NONE, 1, 13, 0},
    {"keypad Enter", KP(ENTER), KMOD_NONE, 1, 13, 0},
    {"Ctrl-Enter", KEY(RETURN), KMOD_LCTRL, 1, 10, 0},
    {"Escape", KEY(ESCAPE), KMOD_NONE, 1, 27, 0},
    {"Backspace", KEY(BACKSPACE), KMOD_NONE, 1, 8, 0},
    {"Ctrl-Backspace", KEY(BACKSPACE), KMOD_LCTRL, 1, 127, 0},
    {"Tab", KEY(TAB), KMOD_NONE, 1, 9, 0},
    {"Shift-Tab", KEY(TAB), KMOD_LSHIFT, 2, 0, 0x0f},
    {"Alt-Escape (enhanced only)", KEY(ESCAPE), KMOD_LALT, 0, 0, 0},
    {"Shift-Escape", KEY(ESCAPE), KMOD_LSHIFT, 1, 27, 0},
    {"Alt-Backspace (enhanced only)", KEY(BACKSPACE), KMOD_LALT, 0, 0, 0},
    {"Ctrl-Tab (enhanced only)", KEY(TAB), KMOD_LCTRL, 0, 0, 0},
    {"Alt-Tab (enhanced only)", KEY(TAB), KMOD_LALT, 0, 0, 0},
    {"Ctrl-keypad Enter", KP(ENTER), KMOD_LCTRL, 1, 10, 0},
    /* The cursor keys: party picks, lists, Move in 3D and overland, the
     * combat directions, Modify's fields, Rest and Speed. */
    {"Up", KEY(UP), KMOD_NONE, 2, 0, 0x48},
    {"Down", KEY(DOWN), KMOD_NONE, 2, 0, 0x50},
    {"Left", KEY(LEFT), KMOD_NONE, 2, 0, 0x4b},
    {"Right", KEY(RIGHT), KMOD_NONE, 2, 0, 0x4d},
    {"Home", KEY(HOME), KMOD_NONE, 2, 0, 0x47},
    {"End", KEY(END), KMOD_NONE, 2, 0, 0x4f},
    {"PgUp", KEY(PAGEUP), KMOD_NONE, 2, 0, 0x49},
    {"PgDn", KEY(PAGEDOWN), KMOD_NONE, 2, 0, 0x51},
    {"Ins", KEY(INSERT), KMOD_NONE, 2, 0, 0x52},
    {"Del (Modify's name)", KEY(DELETE), KMOD_NONE, 2, 0, 0x53},
    {"Shift-Up", KEY(UP), KMOD_LSHIFT, 2, 0, 0x48},
    {"Ctrl-Left", KEY(LEFT), KMOD_LCTRL, 2, 0, 0x73},
    {"Ctrl-Right", KEY(RIGHT), KMOD_RCTRL, 2, 0, 0x74},
    {"Ctrl-Home", KEY(HOME), KMOD_LCTRL, 2, 0, 0x77},
    {"Ctrl-End", KEY(END), KMOD_LCTRL, 2, 0, 0x75},
    {"Ctrl-PgUp", KEY(PAGEUP), KMOD_LCTRL, 2, 0, 0x84},
    {"Ctrl-PgDn", KEY(PAGEDOWN), KMOD_LCTRL, 2, 0, 0x76},
    {"Ctrl-Up (enhanced only)", KEY(UP), KMOD_LCTRL, 0, 0, 0},
    {"Alt-Up (enhanced only)", KEY(UP), KMOD_LALT, 0, 0, 0},
    /* The keypad with NumLock on (3e99:005b): digits, which menus take as
     * directions (DS:1fdf: 5 is 0x20, combat's centre), and Shift gives
     * the cursor keys. */
    {"keypad 1", KP(1), KMOD_NONE, 1, '1', 0},
    {"keypad 2", KP(2), KMOD_NONE, 1, '2', 0},
    {"keypad 3", KP(3), KMOD_NONE, 1, '3', 0},
    {"keypad 4", KP(4), KMOD_NONE, 1, '4', 0},
    {"keypad 5", KP(5), KMOD_NONE, 1, '5', 0},
    {"keypad 6", KP(6), KMOD_NONE, 1, '6', 0},
    {"keypad 7", KP(7), KMOD_NONE, 1, '7', 0},
    {"keypad 8", KP(8), KMOD_NONE, 1, '8', 0},
    {"keypad 9", KP(9), KMOD_NONE, 1, '9', 0},
    {"keypad 0", KP(0), KMOD_NONE, 1, '0', 0},
    {"keypad .", KP(PERIOD), KMOD_NONE, 1, '.', 0},
    {"keypad 8, NumLock off on the host", KP(8), KMOD_NONE, 1, '8', 0},
    {"Shift-keypad 8", KP(8), KMOD_LSHIFT, 2, 0, 0x48},
    {"Shift-keypad 2", KP(2), KMOD_LSHIFT, 2, 0, 0x50},
    {"Shift-keypad 4", KP(4), KMOD_LSHIFT, 2, 0, 0x4b},
    {"Shift-keypad 6", KP(6), KMOD_LSHIFT, 2, 0, 0x4d},
    {"Shift-keypad 7", KP(7), KMOD_LSHIFT, 2, 0, 0x47},
    {"Shift-keypad 9", KP(9), KMOD_LSHIFT, 2, 0, 0x49},
    {"Shift-keypad 1", KP(1), KMOD_LSHIFT, 2, 0, 0x4f},
    {"Shift-keypad 3", KP(3), KMOD_LSHIFT, 2, 0, 0x51},
    {"Shift-keypad 0", KP(0), KMOD_LSHIFT, 2, 0, 0x52},
    {"Shift-keypad .", KP(PERIOD), KMOD_LSHIFT, 2, 0, 0x53},
    {"Shift-keypad 5", KP(5), KMOD_LSHIFT, 0, 0, 0},
    {"Ctrl-keypad 7", KP(7), KMOD_LCTRL, 2, 0, 0x77},
    {"keypad /", KP(DIVIDE), KMOD_NONE, 1, '/', 0},
    {"keypad *", KP(MULTIPLY), KMOD_NONE, 1, '*', 0},
    {"keypad -", KP(MINUS), KMOD_NONE, 1, '-', 0},
    {"keypad +", KP(PLUS), KMOD_NONE, 1, '+', 0},
    /* Alt and letters: combat's Alt-Q and Alt-A (Auto), Alt-R, Alt-M
     * (Magic), Alt-X (the Gods, with Helm); the rest for the scan codes'
     * aliases. */
    {"Alt-Q", CH('q', Q), KMOD_LALT, 2, 0, 0x10},
    {"Alt-Shift-Q", CH('q', Q), KMOD_LALT | KMOD_LSHIFT, 2, 0, 0x10},
    {"Alt-A", CH('a', A), KMOD_LALT, 2, 0, 0x1e},
    {"Alt-R", CH('r', R), KMOD_RALT, 2, 0, 0x13},
    {"Alt-M", CH('m', M), KMOD_LALT, 2, 0, 0x32},
    {"Alt-X", CH('x', X), KMOD_LALT, 2, 0, 0x2d},
    {"Alt-D (scan 0x20, as keypad 5)", CH('d', D), KMOD_LALT, 2, 0, 0x20},
    {"Alt-P", CH('p', P), KMOD_LALT, 2, 0, 0x19},
    {"Alt-L", CH('l', L), KMOD_LALT, 2, 0, 0x26},
    {"Alt-Z", CH('z', Z), KMOD_LALT, 2, 0, 0x2c},
    {"Alt-B (0x30, a digit to the money amount)", CH('b', B), KMOD_LALT, 2, 0, 0x30},
    {"Alt-1", CH('1', 1), KMOD_LALT, 2, 0, 0x78},
    {"Alt-0", CH('0', 0), KMOD_LALT, 2, 0, 0x81},
    {"Alt--", CH('-', MINUS), KMOD_LALT, 2, 0, 0x82},
    {"Alt-=", CH('=', EQUALS), KMOD_LALT, 2, 0, 0x83},
    {"Alt-Space", KEY(SPACE), KMOD_LALT, 1, ' ', 0},
    {"Alt-Enter (enhanced only: the front end's fullscreen)", KEY(RETURN), KMOD_LALT, 0, 0, 0},
    /* Ctrl and letters (Ctrl-C, Ctrl-D, Ctrl-S, Ctrl-Z of 1614:025b). */
    {"Ctrl-A", CH('a', A), KMOD_LCTRL, 1, 1, 0},
    {"Ctrl-C", CH('c', C), KMOD_LCTRL, 1, 3, 0},
    {"Ctrl-D", CH('d', D), KMOD_LCTRL, 1, 4, 0},
    {"Ctrl-S", CH('s', S), KMOD_RCTRL, 1, 0x13, 0},
    {"Ctrl-Z", CH('z', Z), KMOD_LCTRL, 1, 0x1a, 0},
    {"Ctrl-[", CH('[', LEFTBRACKET), KMOD_LCTRL, 1, 27, 0},
    {"Ctrl-2", CH('2', 2), KMOD_LCTRL, 2, 0, 0x03},
    {"Ctrl-6", CH('6', 6), KMOD_LCTRL, 1, 30, 0},
    {"Ctrl-1", CH('1', 1), KMOD_LCTRL, 0, 0, 0},
    {"Ctrl-Alt-Q", CH('q', Q), KMOD_LCTRL | KMOD_LALT, 0, 0, 0},
    /* Function keys: F10 the title's Demo; F7-F10 and Shift-F1, F3, F6 as
     * the letters A-D, T, V, Y where menus take scan codes as letters
     * (saved games A-J); Ctrl-F8 the overland map's camp. */
    {"F1", KEY(F1), KMOD_NONE, 2, 0, 0x3b},
    {"F7", KEY(F7), KMOD_NONE, 2, 0, 0x41},
    {"F10", KEY(F10), KMOD_NONE, 2, 0, 0x44},
    {"Shift-F1", KEY(F1), KMOD_LSHIFT, 2, 0, 0x54},
    {"Shift-F6", KEY(F6), KMOD_LSHIFT, 2, 0, 0x59},
    {"Ctrl-F1", KEY(F1), KMOD_LCTRL, 2, 0, 0x5e},
    {"Ctrl-F8", KEY(F8), KMOD_LCTRL, 2, 0, 0x65},
    {"Alt-F1", KEY(F1), KMOD_LALT, 2, 0, 0x68},
    {"Alt-F10", KEY(F10), KMOD_LALT, 2, 0, 0x71},
    {"F11 (enhanced only: fullscreen)", KEY(F11), KMOD_NONE, 0, 0, 0},
    {"F12 (enhanced only: screenshot)", KEY(F12), KMOD_NONE, 0, 0, 0},
    /* Keys the game never sees. */
    {"GUI-a", CH('a', A), KMOD_LGUI, 0, 0, 0},
    {"AltGr-q", CH('q', Q), KMOD_MODE, 0, 0, 0},
    {"a non-ASCII key", (SDL_Keycode)0xe9, SDL_SCANCODE_2, KMOD_NONE, 0, 0, 0},
    {"Left Shift alone", KEY(LSHIFT), KMOD_LSHIFT, 0, 0, 0},
};

static void test_table(void)
{
    for (size_t i = 0; i < sizeof table / sizeof table[0]; ++i) {
        const expect *e = &table[i];
        int codes[2] = {-9, -9};
        int count = cok_keymap_codes(e->key, e->scancode, e->mod, codes);
        if (count != e->count || (count >= 1 && codes[0] != e->code) ||
            (count == 2 && codes[1] != e->scan)) {
            fprintf(stderr, "%s: got %d codes %d %d\n", e->what, count, codes[0], codes[1]);
            exit(1);
        }
    }
}

/* Every code the port reads can be typed, and a key script's codes come
 * back through the key found for them. */
static void test_find(void)
{
    /* Every scan code function 0 gives. */
    static const int scans[] = {
        0x03, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1e, 0x1f, 0x20,
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x3b, 0x3c,
        0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x47, 0x48, 0x49, 0x4b, 0x4d, 0x4f, 0x50,
        0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e,
        0x6f, 0x70, 0x71, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e,
        0x7f, 0x80, 0x81, 0x82, 0x83, 0x84,
    };
    for (int code = 1; code < 128; ++code) {
        /* Ctrl gives 1-31 and 127 (Ctrl-Backspace), keys the rest. */
        SDL_Keysym key;
        if (!cok_keymap_find(code, -1, &key)) {
            fprintf(stderr, "ASCII %d: no key\n", code);
            exit(1);
        }
        int codes[2];
        CHECK(cok_keymap_codes(key.sym, key.scancode, key.mod, codes) == 1 && codes[0] == code);
    }
    for (size_t i = 0; i < sizeof scans / sizeof scans[0]; ++i) {
        SDL_Keysym key;
        if (!cok_keymap_find(0, scans[i], &key)) {
            fprintf(stderr, "scan %02x: no key\n", scans[i]);
            exit(1);
        }
        int codes[2];
        CHECK(cok_keymap_codes(key.sym, key.scancode, key.mod, codes) == 2 && codes[0] == 0 &&
              codes[1] == scans[i]);
    }
    /* Scan codes no key gives through function 0: NumLock, ScrollLock,
     * the keypad's - and + as extended keys, enhanced keys. */
    SDL_Keysym key;
    CHECK(!cok_keymap_find(0, 0x45, &key) && !cok_keymap_find(0, 0x46, &key));
    CHECK(!cok_keymap_find(0, 0x4a, &key) && !cok_keymap_find(0, 0x4e, &key));
    CHECK(!cok_keymap_find(0, 0x85, &key) && !cok_keymap_find(0, 0x86, &key));
    CHECK(!cok_keymap_find(0x80, -1, &key) && !cok_keymap_find(0, 0x4c, &key));
    /* The main block is found before the keypad. */
    CHECK(cok_keymap_find('1', -1, &key) && key.scancode == SDL_SCANCODE_1);
    CHECK(cok_keymap_find(0, 0x48, &key) && key.scancode == SDL_SCANCODE_UP && key.mod == 0);
    CHECK(cok_keymap_find('E', -1, &key) && key.scancode == SDL_SCANCODE_E &&
          key.mod == KMOD_LSHIFT);
    CHECK(cok_keymap_find(0, 0x10, &key) && key.sym == 'q' && key.mod == KMOD_LALT);
}

/* eclplay's key escapes, which cok's --keys shares. */
static void test_script(void)
{
    cok_key_script s = cok_key_script_start("a\\r\\e\\b\\<\\>\\^\\v\\@q\\@x\\#44\\t\\\\\\kZ\\@1\\#4");
    static const int expected[] = {'a', 13, 27, 8, 0, 0x4b, 0, 0x4d, 0, 0x48, 0, 0x50,
                                   0, 0x10, 0, 0x2d, 0, 0x44, COK_KEY_TIMEOUT, '\\',
                                   'Z', '@', '1', '#', '4', -1, -1};
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; ++i) {
        int code = cok_key_script_read(&s);
        if (code != expected[i]) {
            fprintf(stderr, "script code %zu: %d, not %d\n", i, code, expected[i]);
            exit(1);
        }
    }
    CHECK(cok_key_script_done(&s));
    /* \k: a poll finds the key after it, which a read then gives. */
    s = cok_key_script_start("x\\ky");
    CHECK(!cok_key_script_poll(&s));
    CHECK(cok_key_script_read(&s) == 'x');
    CHECK(cok_key_script_poll(&s) && !cok_key_script_poll(&s));
    CHECK(cok_key_script_read(&s) == 'y' && cok_key_script_done(&s));
    s = cok_key_script_start("\\@a");
    CHECK(cok_key_script_read(&s) == 0 && cok_key_script_owed(&s) && !cok_key_script_done(&s));
    CHECK(cok_key_script_read(&s) == 0x1e && !cok_key_script_owed(&s));
    s = cok_key_script_start(NULL);
    CHECK(cok_key_script_done(&s) && cok_key_script_read(&s) == -1);
    /* Typed ahead: a group, at once, when the script reaches it. */
    cok_keystroke k[COK_KEYS_BUFFERED];
    s = cok_key_script_start("a\\[b\\v\\@q\\]c");
    CHECK(cok_key_script_ahead(&s, false, k, 15) == 0 && cok_key_script_read(&s) == 'a');
    CHECK(cok_key_script_ahead(&s, false, k, 15) == 3);
    CHECK(k[0].code == 'b' && k[0].scan == -1 && k[1].code == 0 && k[1].scan == 0x50);
    CHECK(k[2].code == 0 && k[2].scan == 0x10 && !cok_key_script_owed(&s));
    CHECK(cok_key_script_read(&s) == 'c' && cok_key_script_done(&s));
    /* More than fit are lost; \w holds a group back for that many of the
     * game's waits, but a read takes it at once. */
    s = cok_key_script_start("\\[xyz\\]q");
    CHECK(cok_key_script_ahead(&s, true, k, 2) == 2 && k[1].code == 'y');
    CHECK(cok_key_script_read(&s) == 'q');
    s = cok_key_script_start("\\w3.\\[x\\]");
    CHECK(cok_key_script_ahead(&s, false, k, 15) == 0);
    cok_key_script_tick(&s);
    cok_key_script_tick(&s);
    CHECK(cok_key_script_ahead(&s, false, k, 15) == 0);
    cok_key_script_tick(&s);
    CHECK(cok_key_script_ahead(&s, false, k, 15) == 1 && k[0].code == 'x');
    s = cok_key_script_start("\\w3.\\[x\\]");
    CHECK(cok_key_script_ahead(&s, true, k, 15) == 1);
    s = cok_key_script_start("\\w3.\\[x\\]y");
    CHECK(cok_key_script_read(&s) == 'x' && cok_key_script_read(&s) == 'y');
    /* \p: a pause in hundredths, which reads pass over. */
    unsigned long pause;
    s = cok_key_script_start("\\p50.y\\p7.z");
    CHECK(cok_key_script_pause(&s, &pause) && pause == 50 && !cok_key_script_pause(&s, &pause));
    CHECK(cok_key_script_read(&s) == 'y' && cok_key_script_read(&s) == 'z');
}

int main(void)
{
    test_table();
    test_find();
    test_script();
    puts("keymap tests passed");
    return 0;
}
