#ifndef COK_CONFIG_H
#define COK_CONFIG_H

#include "sound.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* KRYNN.CFG, as startup reads it (11c7:031a): Read(f, c) and ReadLn for
 * the first line, the display (DS:4b76): 'E' EGA, 'T' Tandy, any other
 * character CGA (the setup's [1] CGA writes 'C'); then Read(f, c) for the
 * second, the sound device (DS:577a): 'C', 'R', 'A', 'T' or 'P' give 0-4,
 * any other character 5, none. Upper case only, nothing skipped before
 * them. Turbo Pascal's text reads end a line at a CR, a LF after it
 * skipped, and stop at the end or a Ctrl-Z, where Read gives Ctrl-Z: so an
 * empty file is CGA, and an empty second line, a single line or LF-only
 * line ends (the whole file one line) give no sound. */
/* The display mode, DS:4b76, as startup sets it from KRYNN.CFG's first line
 * (11c7:031a): CGA (video mode 4, 320x200 in 4 colours), EGA (mode 0xd) or
 * the Tandy's mode 9, both 16 colours. */
typedef enum { COK_DISPLAY_CGA, COK_DISPLAY_EGA, COK_DISPLAY_TANDY } cok_display;

typedef struct {
    cok_display display;
    cok_sound_device sound;
} cok_config;

/* "CGA", "EGA" or "Tandy". */
const char *cok_display_name(cok_display display);

cok_config cok_config_parse(const uint8_t *text, size_t size);
/* Read ASSETS/KRYNN.CFG (or krynn.cfg) into *config. Returns false, config
 * unchanged, when there is none or it cannot be read: the original then
 * asks for the graphics, sound and input (11c7:031a's screens) and writes
 * the file; the port does neither, and the caller chooses. */
bool cok_config_read(const char *assets, cok_config *config);
/* The front ends' --display ega|tandy|cga and --sound tandy|speaker|off:
 * false, *out unchanged, for another name. */
bool cok_config_display_option(const char *name, cok_display *out);
bool cok_config_sound_option(const char *name, cok_sound_device *out);
/* Say on standard error, for what ("display", "sound"), that ASSETS has no
 * KRYNN.CFG and the Tandy is taken. */
void cok_config_missing(const char *what, const char *assets);

#endif
