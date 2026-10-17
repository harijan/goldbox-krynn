#ifndef COK_SETUP_H
#define COK_SETUP_H

#include "config.h"
#include "crt.h"
#include "input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Startup's text screens (11c7:031a), before it sets the graphics mode: the
 * setup that writes KRYNN.CFG when there is none, and the note to Tandy
 * users shown at every start; on the Crt screen (crt.h). */

/* The longest file the setup writes: five lines and fourteen numbers. */
enum { COK_SETUP_FILE_MAX = 512 };

typedef struct {
    /* Crt.Delay (1962:029c), a busy loop that reads no keys. */
    void (*delay)(unsigned ms, void *context);
    void *context;
    /* The game port, for the joystick's calibration (177c:029d); NULL for
     * none. ended, if not NULL, says the run has ended (the calibration's
     * waits stop). */
    const cok_game_port *port;
    const bool *ended;
    /* --input joystick over a file whose third line is not J: the stick
     * is calibrated at startup as the setup's [1] does (177c:029d), see
     * cok_setup_calibrate. */
    bool calibrate;
} cok_setup_hooks;

/* The save path's default (11c7:0388, DS:4ad0 + 'SAVE', DS:4a25, the
 * rules' files.save_dir): the game's directory, or on a floppy (drive A or
 * B) the other drive's root. */
void cok_setup_default_path(const cok_rules *rules, const char *game_dir, char *out,
                            size_t size);

/* The setup (11c7:041c-08a7): four screens, each after ClrScr: the
 * graphics adapter (1-3), the sound (1-6) and the alternate input (1-3),
 * each a digit in range read with ReadKey and echoed (11c7:0020); then
 * the path to save data, read with ReadLn and taken if it is at least 4
 * characters, its second ':' and its first (upper-cased) a drive A-P that
 * is not the game's own floppy, else the default ('USING DEFAULT PATH: '
 * and 2 seconds); upper-cased and ended with '\'. The file's bytes, as it
 * writes them, go to file (at least COK_SETUP_FILE_MAX bytes) and *size:
 * the display C, E or T; the sound C, R, A, T, P or S; the input J, M or
 * N; the path; F (DS:4b26, which nothing reads); then the joystick's
 * fourteen numbers (DS:8996, 89a4): with J, its calibration (177c:029d,
 * on the text screen through hooks->port), else zeros, as without a stick;
 * each line ended with CR LF. Returns false if input ended first. */
bool cok_setup_ask(cok_crt *crt, const cok_keyboard *keys, const cok_setup_hooks *hooks,
                   const cok_rules *rules, const char *game_dir, uint8_t *file, size_t *size);

/* For a front end's --input joystick where the file gives no thresholds
 * (its third line not J): the calibration of the setup's [1] (177c:029d)
 * on the text screen, which, with a stick there, sets config->joystick
 * and its thresholds for the run; the file is not written. False if input
 * ended in it. */
bool cok_setup_calibrate(cok_crt *crt, const cok_setup_hooks *hooks, cok_config *config);

/* The file cannot be made (Rewrite's IOResult, 11c7:040d): the original
 * says so and halts with status 1. */
void cok_setup_write_protected(cok_crt *crt);

/* 11c7:08ac, at every start, with the file or after the setup: ClrScr,
 * 'TANDY 1000 USERS - Make sure NUMLOCK is on.' and Delay(2000). */
void cok_setup_numlock(cok_crt *crt, const cok_setup_hooks *hooks);

typedef enum {
    COK_SETUP_OK,          /* *config holds the file's */
    COK_SETUP_ENDED,       /* input ended in the setup: nothing written */
    COK_SETUP_UNWRITABLE,  /* the file could not be made (error says why) */
    COK_SETUP_UNREADABLE,  /* the file found could not be read */
} cok_setup_status;

/* Startup's text screens as 11c7:031a shows them: with found, the file at
 * path is read; without, the setup runs (in the directory the original
 * runs in, as the port takes it: the rules' files.install_dir) and its
 * file is written at path (made first, as Rewrite does, and removed again
 * if input ends, unless something was there before; with an empty path,
 * nowhere) and read back from its bytes; then the note to Tandy users. Where it fails, the
 * screen ends with the port's own line, "The port stops here: " and
 * error: under the original's message for a file it cannot make. The
 * rows written are logged (crt->log). */
cok_setup_status cok_setup_startup(cok_crt *crt, const cok_keyboard *keys,
                                   const cok_setup_hooks *hooks, const cok_rules *rules,
                                   const char *path, bool found, cok_config *config, char *error,
                                   size_t error_size);

#endif
