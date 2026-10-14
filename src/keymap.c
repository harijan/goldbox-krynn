#include "keymap.h"

#include <string.h>

static int ascii(int codes[2], int code)
{
    codes[0] = code;
    return 1;
}

static int extended(int codes[2], int scan)
{
    codes[0] = 0;
    codes[1] = scan;
    return 2;
}

/* The scan codes of the letters a-z (Alt and a letter gives 0 and this). */
static const unsigned char letter_scans[26] = {
    0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
    0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c,
};

/* The US layout's punctuation, unshifted then shifted. */
static const char plain_marks[] = "`-=[]\\;',./";
static const char shifted_marks[] = "~_+{}|:\"<>?";

/* The keypad's keys with NumLock on, 1-9 then 0 and the point: the digit,
 * and with Shift the cursor key's scan code (0 for 5, which gives none);
 * with Ctrl the scan code INT 16h function 0 gives, or 0. */
static const char keypad_digits[] = "1234567890.";
static const unsigned char keypad_shifted[11] = {0x4f, 0x50, 0x51, 0x4b, 0, 0x4d,
                                                  0x47, 0x48, 0x49, 0x52, 0x53};
static const unsigned char keypad_ctrl[11] = {0x75, 0, 0x76, 0x73, 0, 0x74, 0x77, 0, 0x84, 0, 0};

int cok_keymap_codes(SDL_Keycode key, SDL_Scancode scancode, Uint16 mod, int codes[2])
{
    bool alt = (mod & KMOD_ALT) != 0, ctrl = (mod & KMOD_CTRL) != 0;
    bool shift = (mod & KMOD_SHIFT) != 0, caps = (mod & KMOD_CAPS) != 0;
    if ((mod & (KMOD_GUI | KMOD_MODE)) != 0 || (alt && ctrl)) return 0;

    /* F1-F10: 0x3b on, Shift 0x54, Ctrl 0x5e, Alt 0x68. */
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F10) {
        int i = (int)(scancode - SDL_SCANCODE_F1);
        return extended(codes, (alt ? 0x68 : ctrl ? 0x5e : shift ? 0x54 : 0x3b) + i);
    }
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_PERIOD) {
        int i = (int)(scancode - SDL_SCANCODE_KP_1);
        if (alt) return 0; /* Alt and digits type a character code (cok_keymap_event). */
        if (ctrl) return keypad_ctrl[i] != 0 ? extended(codes, keypad_ctrl[i]) : 0;
        if (shift) return keypad_shifted[i] != 0 ? extended(codes, keypad_shifted[i]) : 0;
        return ascii(codes, keypad_digits[i]);
    }
    switch (scancode) {
    case SDL_SCANCODE_KP_DIVIDE: return alt || ctrl ? 0 : ascii(codes, '/');
    case SDL_SCANCODE_KP_MULTIPLY: return alt || ctrl ? 0 : ascii(codes, '*');
    case SDL_SCANCODE_KP_MINUS: return alt || ctrl ? 0 : ascii(codes, '-');
    case SDL_SCANCODE_KP_PLUS: return alt || ctrl ? 0 : ascii(codes, '+');
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return alt ? 0 : ascii(codes, ctrl ? 10 : 13);
    case SDL_SCANCODE_ESCAPE: return alt ? 0 : ascii(codes, 27);
    case SDL_SCANCODE_BACKSPACE: return alt ? 0 : ascii(codes, ctrl ? 127 : 8);
    case SDL_SCANCODE_TAB: return alt || ctrl ? 0 : shift ? extended(codes, 0x0f) : ascii(codes, 9);
    case SDL_SCANCODE_SPACE: return ascii(codes, ' ');
    /* The cursor keys; with Ctrl those function 0 knows. */
    case SDL_SCANCODE_UP: return alt || ctrl ? 0 : extended(codes, 0x48);
    case SDL_SCANCODE_DOWN: return alt || ctrl ? 0 : extended(codes, 0x50);
    case SDL_SCANCODE_LEFT: return alt ? 0 : extended(codes, ctrl ? 0x73 : 0x4b);
    case SDL_SCANCODE_RIGHT: return alt ? 0 : extended(codes, ctrl ? 0x74 : 0x4d);
    case SDL_SCANCODE_HOME: return alt ? 0 : extended(codes, ctrl ? 0x77 : 0x47);
    case SDL_SCANCODE_END: return alt ? 0 : extended(codes, ctrl ? 0x75 : 0x4f);
    case SDL_SCANCODE_PAGEUP: return alt ? 0 : extended(codes, ctrl ? 0x84 : 0x49);
    case SDL_SCANCODE_PAGEDOWN: return alt ? 0 : extended(codes, ctrl ? 0x76 : 0x51);
    case SDL_SCANCODE_INSERT: return alt || ctrl ? 0 : extended(codes, 0x52);
    case SDL_SCANCODE_DELETE: return alt || ctrl ? 0 : extended(codes, 0x53);
    default: break;
    }

    /* Keys that type a character, by the layout's keycode. */
    if (key >= 'a' && key <= 'z') {
        int i = (int)(key - 'a');
        if (alt) return extended(codes, letter_scans[i]);
        if (ctrl) return ascii(codes, i + 1);
        return ascii(codes, (int)key - (shift != caps ? 32 : 0));
    }
    if (key >= '0' && key <= '9') {
        int d = (int)(key - '0');
        if (alt) return extended(codes, d == 0 ? 0x81 : 0x77 + d);
        if (ctrl) return d == 2 ? extended(codes, 0x03) : d == 6 ? ascii(codes, 30) : 0;
        return ascii(codes, shift ? ")!@#$%^&*("[d] : '0' + d);
    }
    const char *mark = key > 0 && key < 0x80 ? strchr(plain_marks, (int)key) : NULL;
    if (mark == NULL || *mark == '\0') return 0;
    if (alt) return key == '-' ? extended(codes, 0x82) : key == '=' ? extended(codes, 0x83) : 0;
    if (ctrl) {
        switch (key) {
        case '[': return ascii(codes, 27);
        case '\\': return ascii(codes, 28);
        case ']': return ascii(codes, 29);
        case '-': return ascii(codes, 31);
        default: return 0;
        }
    }
    return ascii(codes, shift ? shifted_marks[mark - plain_marks] : (int)key);
}

int cok_keymap_event(cok_keymap_state *state, const SDL_KeyboardEvent *event, int codes[2])
{
    bool down = event->type == SDL_KEYDOWN;
    SDL_Scancode scancode = event->keysym.scancode;
    Uint16 mod = event->keysym.mod;
    if (scancode == SDL_SCANCODE_LALT || scancode == SDL_SCANCODE_RALT) {
        /* Alt's release types the number, if any, and clears it
         * (0040:0019); going down it leaves it. */
        if (down) return 0;
        uint8_t code = state->alt_code;
        state->alt_code = 0;
        return code != 0 ? ascii(codes, code) : 0;
    }
    if (!down) return 0;
    switch (scancode) {
    /* Shift and the lock keys: INT 9 takes them (K16-K22) before Alt's
     * keypad, and they leave its number alone. With Ctrl, NumLock is
     * Pause (K25), which does not turn NumLock and is not ported. */
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: case SDL_SCANCODE_CAPSLOCK: case SDL_SCANCODE_SCROLLLOCK:
        return 0;
    case SDL_SCANCODE_NUMLOCKCLEAR:
        if (!event->repeat && (mod & KMOD_CTRL) == 0) state->numlock_off = !state->numlock_off;
        return 0;
    default: break;
    }
    bool keypad = scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_PERIOD;
    if ((mod & KMOD_ALT) != 0 && (mod & (KMOD_GUI | KMOD_MODE)) == 0) {
        if (keypad && scancode != SDL_SCANCODE_KP_PERIOD) {
            int digit = scancode == SDL_SCANCODE_KP_0 ? 0 : (int)(scancode - SDL_SCANCODE_KP_1) + 1;
            state->alt_code = (uint8_t)(state->alt_code * 10 + digit);
            return 0;
        }
        /* Alt-space types a space and keeps it (K31); other keys clear
         * it (K33). */
        if (scancode != SDL_SCANCODE_SPACE) state->alt_code = 0;
    }
    if (keypad && state->numlock_off && (mod & (KMOD_ALT | KMOD_CTRL)) == 0)
        mod = (mod & KMOD_SHIFT) != 0 ? (Uint16)(mod & ~KMOD_SHIFT) : (Uint16)(mod | KMOD_LSHIFT);
    return cok_keymap_codes(event->keysym.sym, scancode, mod, codes);
}

/* The keys of a US keyboard, the main block before the keypad, so that a
 * code both give is found on the main block. */
typedef struct {
    SDL_Scancode scancode;
    SDL_Keycode key;
} us_key;

/* SDL's keycode for a key that types no character. */
static SDL_Keycode keycode(SDL_Scancode scancode)
{
    return (SDL_Keycode)((unsigned)scancode | (unsigned)SDLK_SCANCODE_MASK);
}

static size_t us_keys(us_key keys[128])
{
    size_t n = 0;
    for (int i = 0; i < 26; ++i)
        keys[n++] = (us_key){(SDL_Scancode)(SDL_SCANCODE_A + i), (SDL_Keycode)('a' + i)};
    for (int i = 0; i < 10; ++i)
        keys[n++] = (us_key){(SDL_Scancode)(SDL_SCANCODE_1 + i), (SDL_Keycode)("1234567890"[i])};
    static const struct { SDL_Scancode scancode; char key; } marks[11] = {
        {SDL_SCANCODE_GRAVE, '`'},        {SDL_SCANCODE_MINUS, '-'},
        {SDL_SCANCODE_EQUALS, '='},       {SDL_SCANCODE_LEFTBRACKET, '['},
        {SDL_SCANCODE_RIGHTBRACKET, ']'}, {SDL_SCANCODE_BACKSLASH, '\\'},
        {SDL_SCANCODE_SEMICOLON, ';'},    {SDL_SCANCODE_APOSTROPHE, '\''},
        {SDL_SCANCODE_COMMA, ','},        {SDL_SCANCODE_PERIOD, '.'},
        {SDL_SCANCODE_SLASH, '/'},
    };
    for (int i = 0; i < 11; ++i) keys[n++] = (us_key){marks[i].scancode, marks[i].key};
    static const SDL_Scancode others[] = {
        SDL_SCANCODE_RETURN,   SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB,
        SDL_SCANCODE_SPACE,    SDL_SCANCODE_UP,     SDL_SCANCODE_DOWN,      SDL_SCANCODE_LEFT,
        SDL_SCANCODE_RIGHT,    SDL_SCANCODE_HOME,   SDL_SCANCODE_END,       SDL_SCANCODE_PAGEUP,
        SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_INSERT, SDL_SCANCODE_DELETE,
    };
    for (size_t i = 0; i < sizeof others / sizeof others[0]; ++i)
        keys[n++] = (us_key){others[i], keycode(others[i])};
    for (int i = 0; i < 10; ++i) {
        SDL_Scancode f = (SDL_Scancode)(SDL_SCANCODE_F1 + i);
        keys[n++] = (us_key){f, keycode(f)};
    }
    for (int s = SDL_SCANCODE_KP_DIVIDE; s <= SDL_SCANCODE_KP_PERIOD; ++s)
        keys[n++] = (us_key){(SDL_Scancode)s, keycode((SDL_Scancode)s)};
    return n;
}

bool cok_keymap_find(int code, int scan, SDL_Keysym *found)
{
    static const Uint16 mods[4] = {KMOD_NONE, KMOD_LSHIFT, KMOD_LCTRL, KMOD_LALT};
    us_key keys[128];
    size_t n = us_keys(keys);
    for (size_t m = 0; m < 4; ++m) {
        for (size_t i = 0; i < n; ++i) {
            int codes[2];
            int count = cok_keymap_codes(keys[i].key, keys[i].scancode, mods[m], codes);
            if (count == 0 || codes[0] != code || (count == 2) != (code == 0) ||
                (count == 2 && codes[1] != scan))
                continue;
            *found = (SDL_Keysym){keys[i].scancode, keys[i].key, mods[m], 0};
            return true;
        }
    }
    return false;
}
