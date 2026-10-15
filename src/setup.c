#include "setup.h"

#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

/* Concatenate b to a string[max] held in out, as Turbo Pascal's string
 * concatenation (1a46:0b51) and assignment (0ade) cut it. */
static void append(char *out, size_t max, const char *b)
{
    size_t n = strlen(out);
    while (n < max && *b != '\0') out[n++] = *b++;
    out[n] = '\0';
}

void cok_setup_default_path(const char *game_dir, char *out, size_t size)
{
    char base[80] = "";
    /* 11c7:0388: below C, a floppy: the other drive's root (DS:4ad0). */
    if ((unsigned char)game_dir[0] < 'C')
        append(base, 79, game_dir[0] == 'A' ? "B:\\" : "A:\\");
    else
        append(base, 67, game_dir); /* FSplit's DirStr, a string[67] */
    append(base, 79, "SAVE"); /* DS:4a25, a string[79] */
    snprintf(out, size, "%s", base);
}

/* 11c7:0020: ReadKey until a digit from low to high, echoed; returns it,
 * or -1 if input ended. Any key's code counts, an extended key's scan
 * code too (Alt-N's 0x31 is '1'). */
static int read_digit(cok_crt *crt, const cok_keyboard *keys, int low, int high)
{
    for (;;) {
        int key = keys->read(keys->context);
        if (key == COK_KEY_TIMEOUT) continue;
        if (key < 0) return -1;
        if (key < '0' || key > '9' || key - '0' < low || key - '0' > high) continue;
        char c = (char)key;
        cok_crt_write(crt, &c, 1);
        return key - '0';
    }
}

/* Write(f, c); WriteLn(f). */
static void line(uint8_t *file, size_t *size, const char *text)
{
    size_t n = strlen(text);
    memcpy(file + *size, text, n);
    *size += n;
    file[(*size)++] = '\r';
    file[(*size)++] = '\n';
}

static bool in_range(char c, char low, char high)
{
    return c >= low && c <= high;
}

/* UpCase (1a46:17e3): a-z only. */
static char upcase(char c)
{
    return in_range(c, 'a', 'z') ? (char)(c - 0x20) : c;
}

/* 177c:029d's screens: ClrScr and its line; the failure's for 2 seconds
 * (Delay). */
typedef struct {
    cok_crt *crt;
    const cok_setup_hooks *hooks;
} calibration;

static void calibration_prompt(cok_stick_prompt which, void *context)
{
    static const char *const lines[3] = {
        "Push JOYSTICK #1 to the extreme upper left position and press BUTTON #1.",
        "Push JOYSTICK #1 to the extreme lower right position and press BUTTON #1.",
        "Couldn't read the joystick.  Aborting calibration.",
    };
    const calibration *c = context;
    cok_crt_clear(c->crt);
    cok_crt_write_line(c->crt, lines[which]);
    if (which == COK_STICK_FAILED && c->hooks != NULL && c->hooks->delay != NULL)
        c->hooks->delay(2000, c->hooks->context);
}

bool cok_setup_calibrate(cok_crt *crt, const cok_setup_hooks *hooks, cok_config *config)
{
    cok_stick stick = {.on = true};
    calibration c = {crt, hooks};
    cok_stick_calibrate(&stick, hooks != NULL ? hooks->port : NULL, calibration_prompt, &c,
                        hooks != NULL ? hooks->ended : NULL);
    if (hooks != NULL && hooks->ended != NULL && *hooks->ended) return false;
    if (stick.on) {
        config->joystick = true;
        memcpy(config->joystick_x, stick.x, sizeof stick.x);
        memcpy(config->joystick_y, stick.y, sizeof stick.y);
    }
    return true;
}

bool cok_setup_ask(cok_crt *crt, const cok_keyboard *keys, const cok_setup_hooks *hooks,
                   const char *game_dir, uint8_t *file, size_t *size)
{
    *size = 0;
    cok_crt_clear(crt);
    cok_crt_write_string(crt, "GRAPHICS ADAPTER TYPE:  [1] CGA  [2] EGA  [3] Tandy  ");
    int answer = read_digit(crt, keys, 1, 3);
    if (answer < 0) return false;
    line(file, size, answer == 1 ? "C" : answer == 2 ? "E" : "T");

    cok_crt_clear(crt);
    cok_crt_write_line(crt, "SOUND TYPE:  [1] C/MS");
    cok_crt_write_line(crt, "             [2] Roland LAPC");
    cok_crt_write_line(crt, "             [3] AdLib");
    cok_crt_write_line(crt, "             [4] Tandy 1000");
    cok_crt_write_line(crt, "             [5] PC");
    cok_crt_write_line(crt, "             [6] Silent");
    answer = read_digit(crt, keys, 1, 6);
    if (answer < 0) return false;
    static const char *const sounds[6] = {"C", "R", "A", "T", "P", "S"};
    line(file, size, sounds[answer - 1]);

    /* DS:89ce, the joystick, cleared, and set for [1]. */
    cok_crt_clear(crt);
    cok_crt_write_string(crt, "ALTERNATE INPUT TYPE:  [1] JOYSTICK  [2] MOUSE  [3] NEITHER  ");
    answer = read_digit(crt, keys, 1, 3);
    if (answer < 0) return false;
    line(file, size, answer == 1 ? "J" : answer == 2 ? "M" : "N");
    bool joystick = answer == 1;

    cok_crt_clear(crt);
    char fallback[80];
    cok_setup_default_path(game_dir, fallback, sizeof fallback);
    char prompt[256] = "PATH TO SAVE DATA (DEFAULT - ";
    append(prompt, 255, fallback);
    append(prompt, 255, "):  ");
    cok_crt_write_string(crt, prompt);
    char path[82]; /* DS:49c6, a string[80], and room for '\' */
    if (!cok_crt_read_line(crt, keys, path, 80)) return false;
    size_t n = strlen(path);
    path[0] = upcase(path[0]);
    char drive = path[0];
    /* 11c7:0728: at least "X:" and two more, X a drive from A to P, not
     * the floppy the game runs on (sets at 11c7:0242 and 0262). */
    if (n < 4 || path[1] != ':' || (in_range(drive, 'A', 'B') && drive == game_dir[0]) ||
        !in_range(drive, 'A', 'P')) {
        snprintf(path, sizeof path, "%s", fallback);
        char text[256] = "USING DEFAULT PATH: ";
        append(text, 255, path);
        cok_crt_write_line(crt, text);
        if (hooks != NULL && hooks->delay != NULL) hooks->delay(2000, hooks->context);
        n = strlen(path);
    }
    for (size_t i = 0; i < n; ++i) path[i] = upcase(path[i]);
    if (n > 0 && path[n - 1] != '\\') append(path, 80, "\\");
    line(file, size, path);
    line(file, size, "F");
    /* 177c:029d (11c7:0845), then DS:8996[i] and 89a4[i] for i 0-6, as
     * longints: J kept whatever it found. */
    cok_stick stick = {.on = joystick};
    calibration c = {crt, hooks};
    cok_stick_calibrate(&stick, hooks != NULL ? hooks->port : NULL, calibration_prompt, &c,
                        hooks != NULL ? hooks->ended : NULL);
    if (hooks != NULL && hooks->ended != NULL && *hooks->ended) return false;
    for (size_t i = 0; i < 7; ++i) {
        char number[8];
        snprintf(number, sizeof number, "%u", stick.x[i]);
        line(file, size, number);
        snprintf(number, sizeof number, "%u", stick.y[i]);
        line(file, size, number);
    }
    return true;
}

void cok_setup_write_protected(cok_crt *crt)
{
    cok_crt_clear(crt);
    cok_crt_write_line(crt, "Sorry, disk A can't be write-protected.");
    cok_crt_write_line(crt, "");
    cok_crt_write_line(crt, "If these are your masters, use DISKCOPY to make duplicates.");
}

void cok_setup_numlock(cok_crt *crt, const cok_setup_hooks *hooks)
{
    cok_crt_clear(crt);
    cok_crt_write_line(crt, "TANDY 1000 USERS - Make sure NUMLOCK is on.");
    if (hooks != NULL && hooks->delay != NULL) hooks->delay(2000, hooks->context);
}

/* The port's own line under the screen: why the run stops. */
static cok_setup_status stop(cok_crt *crt, cok_setup_status status, const char *error)
{
    cok_crt_write_line(crt, "");
    char text[700];
    snprintf(text, sizeof text, "The port stops here: %s", error);
    cok_crt_write_line(crt, text);
    cok_crt_log_rows(crt);
    return status;
}

cok_setup_status cok_setup_startup(cok_crt *crt, const cok_keyboard *keys,
                                   const cok_setup_hooks *hooks, const char *path, bool found,
                                   cok_config *config, char *error, size_t error_size)
{
    error[0] = '\0';
    /* 177c:0000 at 11c7:0b16: whether stick 1 reads, for the file's
     * thresholds. */
    uint16_t x, y;
    uint8_t buttons;
    bool stick = cok_stick_read(hooks != NULL ? hooks->port : NULL, 1, &x, &y, &buttons);
    if (found) {
        if (!cok_config_read_file_stick(path, config, stick)) {
            /* A file the original would read (or, unreadable, take as empty:
             * Reset's error unchecked); the port stops rather than guess. */
            snprintf(error, error_size, "%s: %s", path, strerror(errno));
            cok_crt_clear(crt);
            return stop(crt, COK_SETUP_UNREADABLE, error);
        }
    } else {
        /* 11c7:03f4: Assign and Rewrite, before the first question. Only a
         * file the port made is removed again. */
        FILE *f = NULL;
        bool made = false;
        if (path[0] != '\0') {
            struct stat st;
            made = stat(path, &st) != 0;
            f = fopen(path, "wb");
            if (f == NULL) {
                snprintf(error, error_size, "%s: %s", path, strerror(errno));
                cok_setup_write_protected(crt);
                return stop(crt, COK_SETUP_UNWRITABLE, error);
            }
        }
        uint8_t file[COK_SETUP_FILE_MAX];
        size_t size;
        if (!cok_setup_ask(crt, keys, hooks, COK_SETUP_GAME_DIR, file, &size)) {
            /* The original leaves the file empty; the port leaves none. */
            if (f != NULL) {
                fclose(f);
                if (made) remove(path);
            }
            cok_crt_log_rows(crt);
            return COK_SETUP_ENDED;
        }
        if (f != NULL) {
            bool failed = fwrite(file, 1, size, f) != size;
            if (fclose(f) != 0) failed = true;
            if (failed) {
                /* The original's writes go unchecked; the port stops. */
                snprintf(error, error_size, "%s: %s", path, strerror(errno));
                if (made) remove(path);
                return stop(crt, COK_SETUP_UNWRITABLE, error);
            }
        }
        *config = cok_config_parse_stick(file, size, stick);
    }
    if (hooks != NULL && hooks->calibrate && !config->joystick &&
        !cok_setup_calibrate(crt, hooks, config)) {
        cok_crt_log_rows(crt);
        return COK_SETUP_ENDED;
    }
    cok_setup_numlock(crt, hooks);
    cok_crt_log_rows(crt);
    return COK_SETUP_OK;
}
