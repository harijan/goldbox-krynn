#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

cok_config cok_config_parse(const uint8_t *text, size_t size)
{
    cok_config config;
    size_t at = 0;
    switch (read_char(text, size, &at)) { /* DS:4842 */
    case 'E': config.display = COK_DISPLAY_EGA; break;
    case 'T': config.display = COK_DISPLAY_TANDY; break;
    default: config.display = COK_DISPLAY_CGA; break;
    }
    read_line(text, size, &at);
    switch (read_char(text, size, &at)) {
    case 'C': config.sound = COK_SOUND_CMS; break;
    case 'R': config.sound = COK_SOUND_ROLAND; break;
    case 'A': config.sound = COK_SOUND_ADLIB; break;
    case 'T': config.sound = COK_SOUND_TANDY; break;
    case 'P': config.sound = COK_SOUND_SPEAKER; break;
    default: config.sound = COK_SOUND_NONE; break;
    }
    return config;
}

bool cok_config_read(const char *assets, cok_config *config)
{
    static const char *const names[] = {"KRYNN.CFG", "krynn.cfg"};
    for (size_t i = 0; i < 2; ++i) {
        char path[1100];
        snprintf(path, sizeof path, "%.1000s/%s", assets, names[i]);
        FILE *f = fopen(path, "rb");
        if (f == NULL) continue;
        enum { MOST = 65536 };
        uint8_t *text = malloc(MOST);
        size_t size = text != NULL ? fread(text, 1, MOST, f) : 0;
        bool failed = text == NULL || ferror(f) != 0;
        fclose(f);
        if (!failed) *config = cok_config_parse(text, size);
        free(text);
        return !failed;
    }
    return false;
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
    else if (strcmp(name, "speaker") == 0) *out = COK_SOUND_SPEAKER;
    else if (strcmp(name, "off") == 0) *out = COK_SOUND_NONE;
    else return false;
    return true;
}

void cok_config_missing(const char *what, const char *assets)
{
    fprintf(stderr, "%s: no KRYNN.CFG in %s, where the original would ask for the devices: "
                    "the Tandy\n", what, assets);
}
