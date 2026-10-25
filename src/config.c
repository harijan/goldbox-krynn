#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char *cok_display_name(cok_display display)
{
    switch (display) {
    case COK_DISPLAY_CGA: return "CGA";
    case COK_DISPLAY_EGA: return "EGA";
    case COK_DISPLAY_TANDY: return "Tandy";
    }
    return "unknown";
}

/* Turbo Pascal's Read(f, c) (1a46:0898): the next character, or Ctrl-Z at
 * the end, where it stays. */
static uint8_t read_char(const uint8_t *text, size_t size, size_t *at)
{
    if (*at >= size) return 0x1a;
    return text[(*at)++];
}

/* ReadLn (1a46:081c): up to and past a CR, and a LF after it; it stops
 * before a Ctrl-Z. */
static void read_line(const uint8_t *text, size_t size, size_t *at)
{
    while (*at < size && text[*at] != 0x1a) {
        if (text[(*at)++] == '\r') {
            if (*at < size && text[*at] == '\n') ++*at;
            return;
        }
    }
}

/* Read(f, s) for a string[max] (1a46:08e1): up to a CR or a Ctrl-Z, which
 * stay, or max characters. */
static void read_string(const uint8_t *text, size_t size, size_t *at, char *out, size_t max)
{
    size_t n = 0;
    while (n < max && *at < size && text[*at] != '\r' && text[*at] != 0x1a)
        out[n++] = (char)text[(*at)++];
    out[n] = '\0';
}

/* Read(f, i) for an integer (1a46:0957): blanks and control characters
 * skipped (not a Ctrl-Z), then the characters up to the next one of them,
 * the first 32 kept, as a longint (1a46:1287): an optional + or -, then
 * decimal digits or $ and hexadecimal ones; none gives 0. Anything else,
 * or more than fits, is error 106 (InOutRes, DS:43ac), which gives 0 and
 * makes every later read of the file do nothing (1a46:0778), Read(f, c)
 * giving Ctrl-Z. Returns false for the error. */
static bool read_integer(const uint8_t *text, size_t size, size_t *at, uint16_t *value)
{
    while (*at < size && text[*at] != 0x1a && text[*at] <= ' ') ++*at;
    char token[32];
    size_t n = 0;
    while (*at < size && text[*at] > ' ') {
        if (n < sizeof token) token[n] = (char)text[*at];
        ++n;
        ++*at;
    }
    if (n > sizeof token) n = sizeof token;
    *value = 0;
    if (n == 0) return true;
    size_t i = 0;
    bool negative = false;
    if (token[0] == '+' || token[0] == '-') {
        negative = token[0] == '-';
        if (++i == n) return false;
    }
    uint32_t v = 0;
    if (token[i] == '$') {
        if (++i == n) return false;
        for (; i < n; ++i) {
            char c = token[i];
            unsigned digit;
            if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
            else if ((c & ~0x20) >= 'A' && (c & ~0x20) <= 'F') digit = (unsigned)((c & ~0x20) - 'A' + 10);
            else return false;
            if (v >> 28 != 0) return false;
            v = v << 4 | digit;
        }
    } else {
        for (; i < n; ++i) {
            if (token[i] < '0' || token[i] > '9') return false;
            if (v & 0xf0000000u) return false;
            v = v * 10 + (uint32_t)(token[i] - '0');
            if (v & 0x80000000u) return false;
        }
    }
    if (negative) v = 0u - v;
    *value = (uint16_t)v;
    return true;
}

cok_config cok_config_parse(const uint8_t *text, size_t size)
{
    return cok_config_parse_stick(text, size, false);
}

static cok_config parse(const uint8_t *text, size_t size, bool stick, const cok_rules *rules);

cok_config cok_config_parse_stick(const uint8_t *text, size_t size, bool stick)
{
    return parse(text, size, stick, &cok_rules_champions);
}

cok_config cok_config_parse_game(const cok_rules *rules, const uint8_t *text, size_t size,
                                 bool stick)
{
    return parse(text, size, stick, rules);
}

static cok_config parse(const uint8_t *text, size_t size, bool stick, const cok_rules *rules)
{
    cok_config config = {0};
    size_t at = 0;
    switch (read_char(text, size, &at)) { /* DS:4842 */
    case 'E': config.display = COK_DISPLAY_EGA; break;
    case 'T': config.display = COK_DISPLAY_TANDY; break;
    default: config.display = COK_DISPLAY_CGA; break;
    }
    read_line(text, size, &at);
    /* The rules' setup.sound_devices: CMS, Roland, AdLib, Tandy and the
     * speaker by letter (11c7:0a5a); any other no sound. */
    uint8_t letter = read_char(text, size, &at);
    config.sound = COK_SOUND_NONE;
    for (unsigned i = 0; i < 5; ++i)
        if (rules->setup.sound_devices[i] != 0 && letter == (uint8_t)rules->setup.sound_devices[i])
            config.sound = (cok_sound_device)i;
    read_line(text, size, &at);
    config.input = (char)read_char(text, size, &at);
    switch ((uint8_t)config.input) {
    case 'J': config.joystick = true; config.mouse = false; break;
    case 'M': config.mouse = true; config.joystick = false; break;
    case 'N': config.mouse = config.joystick = false; break;
    default: break;
    }
    read_line(text, size, &at);
    read_string(text, size, &at, config.save_path, sizeof config.save_path - 1);
    read_line(text, size, &at);
    /* dkk 1187:011b: Champions' saved games' path (DS:99a4) after its own. */
    for (unsigned i = 1; i < rules->files.config_paths; ++i) {
        read_string(text, size, &at, config.other_path, sizeof config.other_path - 1);
        read_line(text, size, &at);
    }
    config.f_line = read_char(text, size, &at) == 'F';
    read_line(text, size, &at);
    /* 177c:0000 (11c7:0b16): the stick's thresholds cleared and, with the
     * joystick chosen, DS:89ce set to whether stick 1 can be read; only
     * then are they read. */
    config.joystick = config.joystick && stick;
    if (config.joystick) {
        for (size_t i = 0; i < 7; ++i) {
            if (!read_integer(text, size, &at, &config.joystick_x[i])) break;
            read_line(text, size, &at);
            if (!read_integer(text, size, &at, &config.joystick_y[i])) break;
            read_line(text, size, &at);
        }
    }
    return config;
}

bool cok_config_read_file(const char *path, cok_config *config)
{
    return cok_config_read_file_stick(path, config, false);
}

static bool read_file(const char *path, cok_config *config, bool stick, const cok_rules *rules);

bool cok_config_read_file_stick(const char *path, cok_config *config, bool stick)
{
    return read_file(path, config, stick, &cok_rules_champions);
}

bool cok_config_read_file_game(const cok_rules *rules, const char *path, cok_config *config,
                               bool stick)
{
    return read_file(path, config, stick, rules);
}

static bool read_file(const char *path, cok_config *config, bool stick, const cok_rules *rules)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    enum { MOST = 65536 };
    uint8_t *text = malloc(MOST);
    size_t size = text != NULL ? fread(text, 1, MOST, f) : 0;
    bool failed = text == NULL || ferror(f) != 0;
    fclose(f);
    if (!failed) *config = parse(text, size, stick, rules);
    free(text);
    return !failed;
}

/* Whether path is a regular file, as 169c:04c1's FindFirst with no
 * attributes finds one: a directory (or a device) counts as missing,
 * which the setup then fails to make, as the original's Rewrite does. A
 * file the port cannot read is found all the same. */
static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool cok_config_find_named(const char *name, const char *dir, char *path, size_t size)
{
    char upper[64], lower[64];
    snprintf(upper, sizeof upper, "%s", name);
    snprintf(lower, sizeof lower, "%s", name);
    for (char *c = upper; *c != '\0'; ++c) *c = (char)toupper((unsigned char)*c);
    for (char *c = lower; *c != '\0'; ++c) *c = (char)tolower((unsigned char)*c);
    const char *const names[] = {upper, lower};
    for (size_t i = 0; i < 2; ++i) {
        snprintf(path, size, "%s/%s", dir, names[i]);
        if (exists(path)) return true;
    }
    return false;
}

/* DIR/KRYNN.CFG or DIR/krynn.cfg (the rules' name, then in lower case),
 * whichever exists, into path. */
static bool find_in(const cok_rules *rules, const char *dir, char *path, size_t size)
{
    return cok_config_find_named(rules->files.config, dir, path, size);
}

/* Whether a and b name the same directory. */
static bool same_dir(const char *a, const char *b)
{
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev &&
           sa.st_ino == sb.st_ino;
}

bool cok_config_find(const cok_rules *rules, const char *option, const char *saves,
                     const char *assets, char *path, char *write_path, size_t size)
{
    if (option != NULL) {
        snprintf(path, size, "%s", option);
        snprintf(write_path, size, "%s", option);
        return exists(path);
    }
    /* Never into the assets directory, --saves naming it too. */
    bool writable = saves != NULL && (assets == NULL || !same_dir(saves, assets));
    if (saves != NULL && find_in(rules, saves, path, size)) {
        snprintf(write_path, size, "%s", writable ? path : "");
        return true;
    }
    if (writable)
        snprintf(write_path, size, "%s/%s", saves, rules->files.config);
    else
        write_path[0] = '\0';
    if (assets != NULL && find_in(rules, assets, path, size)) return true;
    snprintf(path, size, "%s", write_path);
    return false;
}

bool cok_config_rewrite(const char *from, const char *to, char input, const uint16_t x[7],
                        const uint16_t y[7])
{
    /* Each pass of its loop ReadLns lines 1-5 into string[255]s. With no
     * file Reset fails and leaves InOutRes set (nothing calls IOResult):
     * Rewrite still makes the file, but every Write after it does nothing
     * (1a46:07b8), so the file is left empty. (InOutRes stays set for the
     * game's next unchecked I/O, which the port does not model.) */
    char lines[5][256] = {{0}};
    FILE *f = fopen(from, "rb");
    if (f == NULL) {
        f = fopen(to, "wb");
        return f != NULL && fclose(f) == 0;
    }
    {
        enum { MOST = 65536 };
        uint8_t *text = malloc(MOST);
        size_t size = text != NULL ? fread(text, 1, MOST, f) : 0;
        fclose(f);
        size_t at = 0;
        for (size_t i = 0; i < 5 && text != NULL; ++i) {
            read_string(text, size, &at, lines[i], 255);
            read_line(text, size, &at);
        }
        free(text);
    }
    lines[2][0] = input;
    lines[2][1] = '\0';
    f = fopen(to, "wb");
    if (f == NULL) return false;
    for (size_t i = 0; i < 5; ++i) fprintf(f, "%s\r\n", lines[i]);
    /* The stick's thresholds as longints (4def:612f, 6154), or zeros. */
    for (size_t i = 0; i < 7; ++i)
        fprintf(f, "%u\r\n%u\r\n", x != NULL ? x[i] : 0u, y != NULL ? y[i] : 0u);
    bool failed = ferror(f) != 0;
    return fclose(f) == 0 && !failed;
}

bool cok_config_read(const cok_rules *rules, const char *assets, cok_config *config)
{
    char path[1100];
    return find_in(rules, assets, path, sizeof path) &&
           cok_config_read_file_game(rules, path, config, false);
}

bool cok_config_display_option(const char *name, cok_display *out)
{
    static const char *const names[] = {"cga", "ega", "tandy"};
    for (int i = 0; i < 3; ++i)
        if (strcmp(name, names[i]) == 0) {
            *out = (cok_display)i;
            return true;
        }
    return false;
}

bool cok_config_sound_option(const char *name, cok_sound_device *out)
{
    if (strcmp(name, "tandy") == 0) *out = COK_SOUND_TANDY;
    else if (strcmp(name, "adlib") == 0) *out = COK_SOUND_ADLIB;
    else if (strcmp(name, "speaker") == 0) *out = COK_SOUND_SPEAKER;
    else if (strcmp(name, "off") == 0) *out = COK_SOUND_NONE;
    else return false;
    return true;
}

void cok_config_missing(const cok_rules *rules, const char *what, const char *option,
                        const char *saves, const char *assets)
{
    if (option != NULL)
        fprintf(stderr, "%s: no %s", what, option);
    else if (saves != NULL)
        fprintf(stderr, "%s: no %s in %s or %s", what, rules->files.config, saves, assets);
    else
        fprintf(stderr, "%s: no %s in %s", what, rules->files.config, assets);
    fputs(", where the original would ask for the devices: the Tandy\n", stderr);
}
