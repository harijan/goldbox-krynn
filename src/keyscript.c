#include "keyscript.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

cok_key_script cok_key_script_start(const char *keys)
{
    return (cok_key_script){keys != NULL ? keys : "", -1, 0, false};
}

/* At \XN. for escape X: N, and the script past it. */
static bool count_escape(cok_key_script *s, char escape, unsigned long *count)
{
    if (s->keys[0] != '\\' || s->keys[1] != escape || !isdigit((unsigned char)s->keys[2]))
        return false;
    const char *p = s->keys + 2;
    unsigned long n = 0;
    while (isdigit((unsigned char)*p) && n < 100000000UL) n = n * 10 + (unsigned long)(*p++ - '0');
    if (*p != '.') return false;
    s->keys = p + 1;
    *count = n;
    return true;
}

static bool wait_escape(cok_key_script *s, unsigned long *count)
{
    return count_escape(s, 'w', count);
}

static bool at(const cok_key_script *s, char escape)
{
    return s->keys[0] == '\\' && s->keys[1] == escape;
}

/* The keypad's keys, with NumLock off: the cursor key's scan code (0 for
 * 5, which gives nothing), in the order of "1234567890.". */
static const char keypad_keys[] = "1234567890.";
static const unsigned char keypad_cursor[11] = {0x4f, 0x50, 0x51, 0x4b, 0, 0x4d,
                                                0x47, 0x48, 0x49, 0x52, 0x53};

/* At \dC: C, and the script past it. */
static bool keypad_escape(cok_key_script *s, char *key)
{
    if (!at(s, 'd') || s->keys[2] == '\0' || strchr(keypad_keys, s->keys[2]) == NULL) return false;
    *key = s->keys[2];
    s->keys += 3;
    return true;
}

/* At \aN.: N's digits (at most 11), and the script past it. */
static bool alt_escape(cok_key_script *s, char digits[12])
{
    if (!at(s, 'a') || !isdigit((unsigned char)s->keys[2])) return false;
    const char *p = s->keys + 2;
    size_t n = 0;
    while (isdigit((unsigned char)*p) && n < 11) digits[n++] = *p++;
    digits[n] = '\0';
    if (*p != '.') return false;
    s->keys = p + 1;
    return true;
}

bool cok_key_script_physical(cok_key_script *s, cok_physical_key *key)
{
    if (s->pending >= 0) return false;
    if (at(s, 'n')) {
        s->keys += 2;
        s->numlock_off = !s->numlock_off;
        *key = (cok_physical_key){.kind = COK_KEY_NUMLOCK};
        return true;
    }
    if (keypad_escape(s, &key->keypad)) {
        key->kind = COK_KEY_KEYPAD;
        return true;
    }
    if (alt_escape(s, key->digits)) {
        key->kind = COK_KEY_ALT_NUMBER;
        return true;
    }
    return false;
}

/* The code a physical key types, as the BIOS takes it: -1 for none. */
static int physical_code(cok_key_script *s, const cok_physical_key *key)
{
    switch (key->kind) {
    case COK_KEY_NUMLOCK: return -1;
    case COK_KEY_KEYPAD: {
        if (!s->numlock_off) return (unsigned char)key->keypad;
        int scan = keypad_cursor[strchr(keypad_keys, key->keypad) - keypad_keys];
        if (scan == 0) return -1;
        s->pending = scan;
        return 0;
    }
    case COK_KEY_ALT_NUMBER: {
        unsigned code = 0;
        for (const char *p = key->digits; *p != '\0'; ++p)
            code = (code * 10u + (unsigned)(*p - '0')) & 0xffu;
        return code != 0 ? (int)code : -1;
    }
    }
    return -1;
}

/* One key of the script, the escapes that are not keys skipped. */
static int next(cok_key_script *s)
{
    for (;;) {
        unsigned long ignored;
        cok_physical_key key;
        /* A key pressed while polled is read as any other; a read ends a
         * wait; a group's keys are keys; the keypad's keys that type
         * nothing are passed over. */
        if (at(s, 'k') || at(s, '[') || at(s, ']')) {
            s->keys += 2;
        } else if (cok_key_script_physical(s, &key)) {
            int code = physical_code(s, &key);
            if (code >= 0) return code;
        } else if (!wait_escape(s, &ignored) && !count_escape(s, 'p', &ignored)) {
            break;
        }
    }
    if (*s->keys == '\0') return -1;
    int key = (unsigned char)*s->keys++;
    if (key != '\\' || *s->keys == '\0') return key;
    switch (*s->keys++) {
    case 'r': return 13;
    case 'e': return 27;
    case 'b': return 8;
    case '<': s->pending = 0x4b; return 0;
    case '>': s->pending = 0x4d; return 0;
    case '^': s->pending = 0x48; return 0;
    case 'v': s->pending = 0x50; return 0;
    case 't': return COK_KEY_TIMEOUT;
    case '@': {
        /* Alt and a letter: its key's scan code. */
        static const char letters[] = "QWERTYUIOP\0\0\0\0ASDFGHJKL\0\0\0\0\0ZXCVBNM";
        char letter = *s->keys >= 'a' && *s->keys <= 'z' ? (char)(*s->keys - 32) : *s->keys;
        const char *found = letter != '\0' ? memchr(letters, letter, sizeof letters - 1) : NULL;
        if (found == NULL) return '@';
        ++s->keys;
        s->pending = 0x10 + (int)(found - letters);
        return 0;
    }
    case 'c': {
        /* Ctrl and a letter: 1-26 (Ctrl-D 4, Ctrl-Z 0x1a, Ctrl-C 3). */
        char letter = *s->keys >= 'a' && *s->keys <= 'z' ? (char)(*s->keys - 32) : *s->keys;
        if (letter < 'A' || letter > 'Z') return 'c';
        ++s->keys;
        return letter - 'A' + 1;
    }
    case '#': {
        /* A scan code in two hex digits. */
        unsigned scan;
        if (!isxdigit((unsigned char)s->keys[0]) || !isxdigit((unsigned char)s->keys[1]) ||
            sscanf(s->keys, "%2x", &scan) != 1)
            return '#';
        s->keys += 2;
        s->pending = (int)scan;
        return 0;
    }
    default: return (unsigned char)s->keys[-1];
    }
}

int cok_key_script_read(cok_key_script *s)
{
    if (s->pending >= 0) {
        int key = s->pending;
        s->pending = -1;
        return key;
    }
    s->wait = 0;
    return next(s);
}

bool cok_key_script_owed(const cok_key_script *s)
{
    return s->pending >= 0;
}

bool cok_key_script_poll(cok_key_script *s)
{
    if (s->keys[0] != '\\' || s->keys[1] != 'k') return false;
    s->keys += 2;
    return true;
}

void cok_key_script_tick(cok_key_script *s)
{
    if (s->wait > 0) --s->wait;
}

size_t cok_key_script_ahead(cok_key_script *s, bool now, cok_keystroke *strokes, size_t max)
{
    unsigned long count;
    while (s->wait == 0 && wait_escape(s, &count)) s->wait = count;
    if (now) s->wait = 0;
    if (s->wait > 0 || !at(s, '[')) return 0;
    s->keys += 2;
    size_t n = 0;
    while (*s->keys != '\0' && !at(s, ']')) {
        if (at(s, 'k') || at(s, 'w') || at(s, 'p') || at(s, 't') || at(s, '[')) {
            /* Not keys: dropped inside a group. */
            unsigned long ignored;
            if (!wait_escape(s, &ignored) && !count_escape(s, 'p', &ignored)) s->keys += 2;
            continue;
        }
        cok_physical_key key;
        int code, scan = -1;
        if (cok_key_script_physical(s, &key)) {
            code = physical_code(s, &key);
            if (code < 0) continue; /* NumLock, or a key that types nothing */
        } else {
            code = next(s);
            if (code < 0) break;
        }
        if (code == 0) {
            scan = s->pending >= 0 ? s->pending : 0;
            s->pending = -1;
        }
        if (n < max) strokes[n++] = (cok_keystroke){code, scan};
    }
    if (at(s, ']')) s->keys += 2;
    return n;
}

bool cok_key_script_pause(cok_key_script *s, unsigned long *hundredths)
{
    return count_escape(s, 'p', hundredths);
}

bool cok_key_script_done(const cok_key_script *s)
{
    return s->pending < 0 && s->keys[0] == '\0';
}
