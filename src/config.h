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

/* The rest of the file, as startup reads it on: the third line's first
 * character, the alternate input (J the joystick, DS:89ce; M the mouse,
 * DS:8986; N neither; anything else leaves both as they were, off); the
 * fourth line, the save path (DS:5784, string[255], ReadLn), which the
 * port's --saves replaces; the fifth's first character, F setting DS:4b26,
 * which nothing reads; then 177c:0000 keeps the joystick (DS:89ce) only
 * if stick 1 can be read, and only then are seven pairs of integers read,
 * a line each: its X and Y thresholds (DS:8996, 89a4, a word each). The
 * fields are DS's as 11c7:031a leaves them, on a machine with a stick or
 * without (cok_config_parse_stick); the mouse stays as the file says
 * until the game's start resets the driver (1743:0000 at 3e99:0085,
 * cok_adventure_mouse_startup), which turns it off without one. */
typedef struct {
    cok_display display;
    cok_sound_device sound;
    bool joystick, mouse;
    char save_path[256];
    bool f_line;
    uint16_t joystick_x[7], joystick_y[7];
} cok_config;

/* "CGA", "EGA" or "Tandy". */
const char *cok_display_name(cok_display display);

cok_config cok_config_parse(const uint8_t *text, size_t size);
/* As startup reads it on a machine with a joystick (stick) or without one,
 * as the port's: cok_config_parse is the latter. */
cok_config cok_config_parse_stick(const uint8_t *text, size_t size, bool stick);
/* Read the file at path into *config. Returns false, config unchanged,
 * when it cannot be opened or read. With _stick, as cok_config_parse_stick
 * reads it. */
bool cok_config_read_file(const char *path, cok_config *config);
bool cok_config_read_file_stick(const char *path, cok_config *config, bool stick);
/* Read ASSETS/KRYNN.CFG (or krynn.cfg; the rules' files.config) into
 * *config. Returns false, config unchanged, when there is none or it
 * cannot be read. */
bool cok_config_read(const cok_rules *rules, const char *assets, cok_config *config);
/* Where the port keeps KRYNN.CFG (the rules' files.config), which the
 * original reads and writes in its directory: option (--config FILE) if given; else SAVES/KRYNN.CFG or
 * krynn.cfg, the port's own; else the one in ASSETS, as a copy of the
 * original's directory holds it. saves and assets may be NULL. Returns
 * true with path the file found, false without (path then as write_path).
 * write_path is where the setup and Initialize Mouse/Joystick write it:
 * option, the file found in SAVES, or SAVES/KRYNN.CFG; empty for nowhere
 * (no saves). The port never writes into the assets directory. */
bool cok_config_find(const cok_rules *rules, const char *option, const char *saves,
                     const char *assets, char *path, char *write_path, size_t size);
/* Initialize Mouse/Joystick's rewrite (4def:5cc7): lines 1, 2, 4 and 5 of
 * from as ReadLn reads them into string[255]s (empty if it cannot be
 * read), input ('M', 'J' or 'N') as line 3, then the joystick's fourteen
 * numbers, x[i] and y[i] in turn in decimal, zeros where they are NULL,
 * CR LF after each, into to. Returns false if it cannot be written. */
bool cok_config_rewrite(const char *from, const char *to, char input, const uint16_t x[7],
                        const uint16_t y[7]);
/* The front ends' --display ega|tandy|cga and --sound tandy|speaker|off:
 * false, *out unchanged, for another name. */
bool cok_config_display_option(const char *name, cok_display *out);
bool cok_config_sound_option(const char *name, cok_sound_device *out);
/* Say on standard error, for what ("display", "sound"), that there is no
 * KRYNN.CFG where cok_config_find looked (option, or SAVES and ASSETS) and
 * the Tandy is taken. */
void cok_config_missing(const cok_rules *rules, const char *what, const char *option,
                        const char *saves, const char *assets);

#endif
