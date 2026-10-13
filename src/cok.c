/* Play the game in a window: the original's main loop (1000:0134), its
 * title, start menu and adventure, as eclplay --start-menu runs them, with
 * the screen shown through SDL2 and the keys read from the keyboard. The
 * game code is synchronous: it calls the keyboard and the delay hook and
 * waits in them, so they pump the window's events, show the screen when it
 * has changed and sleep until a key, the time a delay asks for, a timed
 * menu's timeout or the small picture's next frame. */
#include "adventure.h"
#include "keymap.h"
#include "keyscript.h"
#include "start.h"

#include <SDL.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

enum {
    SCREEN_W = 320,
    SCREEN_H = 200,
    SCREEN_BYTES = SCREEN_W * SCREEN_H / 2, /* The Tandy layout, two pixels a byte. */
    KEYS_MAX = COK_KEYS_BUFFERED,           /* The BIOS's buffer holds 15 keystrokes. */
    REST_MS = 10,                           /* A rest's step: GetTime's hundredth. */
    PRESENT_MS = 16,                        /* At most about 60 frames a second. */
};

typedef struct {
    cok_adventure *game;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;  /* The screen, 320x200. */
    SDL_Texture *upscaled; /* It scaled up whole, or NULL without render targets. */
    int up_x, up_y;        /* upscaled's factors across and down. */
    bool crt;              /* Stretch 200 lines to 240, as a 4:3 monitor showed them. */
    bool fullscreen;
    uint8_t shown[SCREEN_BYTES]; /* The screen as last put in the texture. */
    uint8_t on_window[SCREEN_BYTES]; /* As last presented, for --log's check. */
    uint8_t rgb[SCREEN_W * SCREEN_H * 3];
    uint8_t palette[16][3];
    bool uploaded, expose;
    SDL_Rect where;        /* Where the screen was last shown, for --log. */
    Uint64 presented;      /* SDL_GetTicks64 at the last present. */
    Uint64 rest_from;      /* At the last poll of a rest's step. */
    Uint64 owed_from;      /* Since when a present is owed, for --log's check; 0 for none. */

    /* Keystrokes typed and not read yet: an ASCII code, or 0 and a scan
     * code; and the scan code owed after a 0 that was read. */
    int queue[KEYS_MAX][2];
    unsigned head, count;
    int owed;

    bool closing;          /* The window was closed: end the run. */
    bool no_delay;
    bool log;
    /* A key script (--keys), typed through SDL's events as the game asks
     * for keys; then the keyboard if keep_playing. */
    bool scripted, keep_playing;
    cok_key_script script;
    char script_error[64];    /* Why --keys failed, or empty. */
    Uint64 pause_until;       /* The end of a \p pause, or 0. */

    /* The timed menu's start (DS:6e11 set), and the small picture's frame's. */
    bool timing;
    Uint64 timed_from, frame_from;

    const char *shots;     /* --shots: each screen shown as the game waits for a key. */
    unsigned shot;
    bool shot_failed;
    const char *screenshots;
    unsigned screenshot;
} frontend;

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "Play Champions of Krynn in a window, from the title.\n"
            "  --assets DIR    the game's DAX files (default Assets)\n"
            "  --saves DIR     the roster and saved games (default saves, created if\n"
            "                  missing); point it at a copy of SAVE/ to play the\n"
            "                  original's games\n"
            "  --scale N       window size, N times 320x200 (default 3)\n"
            "  --aspect A      4:3 (default) shows the 200 lines as 240, as a 4:3\n"
            "                  monitor did; 1:1 keeps the pixels square\n"
            "  --fullscreen    start full screen (F11 or Alt-Enter toggles it)\n"
            "  --screenshots DIR  where F12 saves the screen as cokNNN.bmp (default\n"
            "                  screenshots, created if missing)\n"
            "  --seed N        start Turbo Pascal's Random from N, not the clock\n"
            "  --woof          start as START Woof does, without the title\n"
            "  --helm          as START with Helm: Area anywhere, the Gods cheat, free\n"
            "                  training\n"
            "  --log           print the game's text, menus and unported calls to\n"
            "                  standard error, as eclplay prints them\n"
            "  --keys KEYS     type KEYS first, in eclplay's escapes (\\[KEYS\\] typed\n"
            "                  ahead, \\wN. waiting), through the window's key events;\n"
            "                  the run ends when they run out\n"
            "  --continue      with --keys, then read the keyboard\n"
            "  --no-delay      do not wait where the game waits (for tests; a rest keeps\n"
            "                  its pace)\n"
            "  --shots DIR     save DIR/NNN.bmp of the window each time the game waits\n"
            "                  for a key and when a battle is set up, as eclplay does\n",
            program);
}

/* The display. */

static SDL_Rect fit(const frontend *f)
{
    int w, h;
    if (SDL_GetRendererOutputSize(f->renderer, &w, &h) != 0) w = SCREEN_W, h = SCREEN_H;
    int aspect_h = f->crt ? 240 : SCREEN_H;
    SDL_Rect r;
    if ((long)w * aspect_h <= (long)h * SCREEN_W) {
        r.w = w;
        r.h = (int)((long)w * aspect_h / SCREEN_W);
    } else {
        r.h = h;
        r.w = (int)((long)h * SCREEN_W / aspect_h);
    }
    r.x = (w - r.w) / 2;
    r.y = (h - r.h) / 2;
    return r;
}

/* Copy the screen into the texture if it changed. */
static bool upload(frontend *f)
{
    const cok_picture *screen = &f->game->screen;
    if (screen->pixels == NULL || screen->frame_size != SCREEN_BYTES) return false;
    if (f->uploaded && memcmp(f->shown, screen->pixels, SCREEN_BYTES) == 0) return false;
    memcpy(f->shown, screen->pixels, SCREEN_BYTES);
    cok_image image = cok_picture_frame(screen, 0);
    cok_image_rgb(&image, f->rgb);
    SDL_UpdateTexture(f->texture, NULL, f->rgb, SCREEN_W * 3);
    f->uploaded = true;
    return true;
}

static int factor(int shown, int size)
{
    int k = (shown + size - 1) / size;
    return k < 1 ? 1 : k > 16 ? 16 : k;
}

/* Draw the screen letterboxed: scaled up by whole numbers across and down
 * to at least the size shown with nearest pixels, then to the size shown
 * smoothly, so that the pixels stay even at any size; at a whole multiple
 * of the screen the second step changes nothing. */
static void render(frontend *f, SDL_Rect *where)
{
    SDL_Rect dst = fit(f);
    SDL_SetRenderDrawColor(f->renderer, 0, 0, 0, 255);
    SDL_RenderClear(f->renderer);
    int kx = factor(dst.w, SCREEN_W), ky = factor(dst.h, SCREEN_H);
    if (f->upscaled != NULL && (f->up_x != kx || f->up_y != ky)) {
        SDL_DestroyTexture(f->upscaled);
        f->upscaled = NULL;
        f->up_x = f->up_y = 0;
    }
    if (f->up_x == 0 && SDL_RenderTargetSupported(f->renderer)) {
        f->upscaled = SDL_CreateTexture(f->renderer, SDL_PIXELFORMAT_RGB888,
                                        SDL_TEXTUREACCESS_TARGET, SCREEN_W * kx, SCREEN_H * ky);
        if (f->upscaled != NULL) {
            SDL_SetTextureScaleMode(f->upscaled, SDL_ScaleModeLinear);
            f->up_x = kx;
            f->up_y = ky;
        }
    }
    if (f->upscaled != NULL && SDL_SetRenderTarget(f->renderer, f->upscaled) == 0) {
        SDL_RenderCopy(f->renderer, f->texture, NULL, NULL);
        SDL_SetRenderTarget(f->renderer, NULL);
        SDL_RenderCopy(f->renderer, f->upscaled, NULL, &dst);
    } else {
        SDL_RenderCopy(f->renderer, f->texture, NULL, &dst);
    }
    if (where != NULL) *where = dst;
}

static void shot_error(frontend *f, const char *format, const char *detail)
{
    fprintf(stderr, format, detail);
    fputc('\n', stderr);
    f->shot_failed = true;
}

/* Read the window back as the screen's pixels and save it as eclplay saves
 * the screen, so that the two can be compared byte for byte. Each pixel
 * is shown where its area's middle falls, which must be one of the 16
 * colours; at a whole multiple of the screen across and down its whole
 * block must be that colour. */
static void save_shown(frontend *f, const SDL_Rect *dst, const char *path)
{
    bool whole = dst->w % SCREEN_W == 0 && dst->h % SCREEN_H == 0;
    int kx = whole ? dst->w / SCREEN_W : 1, ky = whole ? dst->h / SCREEN_H : 1;
    size_t pitch = (size_t)dst->w * 3;
    uint8_t *shown = malloc(pitch * (size_t)dst->h);
    uint8_t *pixels = calloc(SCREEN_BYTES, 1);
    if (shown == NULL || pixels == NULL ||
        SDL_RenderReadPixels(f->renderer, dst, SDL_PIXELFORMAT_RGB24, shown, (int)pitch) != 0) {
        shot_error(f, "%s: cannot read the window back", path);
        free(shown);
        free(pixels);
        return;
    }
    for (int y = 0; y < SCREEN_H && !f->shot_failed; ++y) {
        for (int x = 0; x < SCREEN_W && !f->shot_failed; ++x) {
            const uint8_t *p;
            if (whole) {
                p = shown + (size_t)(y * ky) * pitch + (size_t)(x * kx) * 3;
            } else {
                long mx = ((2L * x + 1) * dst->w) / (2 * SCREEN_W);
                long my = ((2L * y + 1) * dst->h) / (2 * SCREEN_H);
                p = shown + (size_t)my * pitch + (size_t)mx * 3;
            }
            int colour = -1;
            for (int c = 0; c < 16 && colour < 0; ++c)
                if (memcmp(p, f->palette[c], 3) == 0) colour = c;
            for (int dy = 0; dy < ky && colour >= 0; ++dy)
                for (int dx = 0; dx < kx; ++dx)
                    if (memcmp(p + (size_t)dy * pitch + (size_t)dx * 3, p, 3) != 0) colour = -1;
            if (colour < 0) {
                shot_error(f, "%s: the window shows a pixel that is not one of the 16 colours", path);
                break;
            }
            pixels[(y * SCREEN_W + x) / 2] |= (uint8_t)(x % 2 == 0 ? colour << 4 : colour);
        }
    }
    if (!f->shot_failed) {
        cok_image image = {.width = SCREEN_W, .height = SCREEN_H, .pixels = pixels};
        cok_image_status status = cok_image_bmp(&image, path);
        if (status != COK_IMAGE_OK) shot_error(f, "%s", cok_image_status_string(status));
    }
    free(shown);
    free(pixels);
}

/* Whether the window owes a present: the screen has changed since it was
 * last shown, or the window was exposed or resized. */
static bool owed(const frontend *f)
{
    const cok_picture *screen = &f->game->screen;
    bool valid = screen->pixels != NULL && screen->frame_size == SCREEN_BYTES;
    return f->expose ||
           (valid && (!f->uploaded || memcmp(f->shown, screen->pixels, SCREEN_BYTES) != 0));
}

/* Show the screen if a present is owed (or force), at most every
 * PRESENT_MS unless forced; with path, save what the window shows there.
 * A present the rate holds back stays owed, and the waits wake for it
 * (present_due), so that the window never keeps an old screen. */
static void present(frontend *f, bool force, const char *path)
{
    Uint64 now = SDL_GetTicks64();
    if (!force && path == NULL && (!owed(f) || now - f->presented < PRESENT_MS)) return;
    upload(f);
    SDL_Rect dst;
    render(f, &dst);
    if (f->log && memcmp(&dst, &f->where, sizeof dst) != 0) {
        int w, h;
        SDL_GetRendererOutputSize(f->renderer, &w, &h);
        fprintf(stderr, "shown: %dx%d at %d,%d of %dx%d\n", dst.w, dst.h, dst.x, dst.y, w, h);
        f->where = dst;
    }
    if (path != NULL) save_shown(f, &dst, path);
    SDL_RenderPresent(f->renderer);
    f->presented = now;
    if (f->log) memcpy(f->on_window, f->shown, SCREEN_BYTES);
    f->expose = false;
}

/* Milliseconds until an owed present is due, or at least limit. */
static Uint64 present_due(const frontend *f, Uint64 now, Uint64 limit)
{
    if (!owed(f)) return limit;
    Uint64 due = f->presented + PRESENT_MS;
    Uint64 ms = due > now ? due - now : 0;
    return ms < limit ? ms : limit;
}

static void shoot(frontend *f)
{
    if (f->shots == NULL || f->shot_failed) return;
    char path[4096];
    snprintf(path, sizeof path, "%s/%03u.bmp", f->shots, f->shot++);
    present(f, true, path);
}

/* F12: the screen as it is, 320x200, as eclplay's --screen saves it. */
static void screenshot(frontend *f)
{
    char path[4096];
    struct stat st;
    if (mkdir(f->screenshots, 0755) != 0 && errno != EEXIST) {
        perror(f->screenshots);
        return;
    }
    do {
        snprintf(path, sizeof path, "%s/cok%03u.bmp", f->screenshots, f->screenshot++);
    } while (stat(path, &st) == 0 && f->screenshot < 1000);
    cok_image image = cok_picture_frame(&f->game->screen, 0);
    cok_image_status status = cok_image_bmp(&image, path);
    if (status != COK_IMAGE_OK)
        fprintf(stderr, "%s: %s\n", path, cok_image_status_string(status));
    else
        fprintf(stderr, "saved %s\n", path);
}

/* Events. */

static void type_key(frontend *f, const int codes[2], int count)
{
    if (count == 0 || f->count == KEYS_MAX) return; /* A full buffer drops the key. */
    unsigned at = (f->head + f->count++) % KEYS_MAX;
    f->queue[at][0] = codes[0];
    f->queue[at][1] = count == 2 ? codes[1] : -1;
}

static void handle(frontend *f, const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT: f->closing = true; break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_CLOSE) f->closing = true;
        if (e->window.event == SDL_WINDOWEVENT_EXPOSED ||
            e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            f->expose = true;
        break;
    case SDL_KEYDOWN: {
        const SDL_Keysym *k = &e->key.keysym;
        Uint16 mod = k->mod;
        /* Keys the game cannot read (INT 16h function 0 drops them). */
        if (k->scancode == SDL_SCANCODE_F11 ||
            ((mod & KMOD_ALT) && (k->scancode == SDL_SCANCODE_RETURN ||
                                  k->scancode == SDL_SCANCODE_KP_ENTER))) {
            f->fullscreen = !f->fullscreen;
            /* The window's size changing sends SDL_WINDOWEVENT_SIZE_CHANGED,
             * which shows the screen again. */
            SDL_SetWindowFullscreen(f->window, f->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            break;
        }
        if (k->scancode == SDL_SCANCODE_F12) {
            screenshot(f);
            break;
        }
        int codes[2];
        type_key(f, codes, cok_keymap_codes(k->sym, k->scancode, mod, codes));
        break;
    }
    default: break;
    }
}

/* Handle every event waiting; with wait, first wait up to ms for one. */
static void pump(frontend *f, Uint32 ms)
{
    SDL_Event e;
    if (ms > 0 && SDL_WaitEventTimeout(&e, (int)(ms > 1000 ? 1000 : ms))) handle(f, &e);
    while (SDL_PollEvent(&e)) handle(f, &e);
}

/* The scan codes only an enhanced keyboard's INT 16h function 0x10 gives
 * for F11 and F12, which a script may press for the front end's keys. */
enum { SCAN_F11 = 0x85, SCAN_F12 = 0x86 };

/* Press the key that gives code (and scan after a 0) through SDL's events,
 * which pump then turns into the keystroke: as the key's repeat while it
 * is held, if repeat, and with no release yet, if hold. */
static bool press(frontend *f, int code, int scan, bool repeat, bool hold)
{
    SDL_Event e = {0};
    e.type = SDL_KEYDOWN;
    e.key.state = SDL_PRESSED;
    e.key.repeat = repeat;
    e.key.windowID = SDL_GetWindowID(f->window);
    if (code == 0 && (scan == SCAN_F11 || scan == SCAN_F12)) {
        SDL_Scancode key = scan == SCAN_F11 ? SDL_SCANCODE_F11 : SDL_SCANCODE_F12;
        e.key.keysym = (SDL_Keysym){key, SDL_GetKeyFromScancode(key), KMOD_NONE, 0};
    } else if (!cok_keymap_find(code, scan, &e.key.keysym)) {
        snprintf(f->script_error, sizeof f->script_error,
                 code == 0 ? "no key gives scan code %02x" : "no key gives %d",
                 code == 0 ? scan : code);
        return false;
    }
    SDL_PushEvent(&e);
    if (!hold) {
        e.type = SDL_KEYUP;
        e.key.state = SDL_RELEASED;
        e.key.repeat = 0;
        SDL_PushEvent(&e);
    }
    return true;
}

/* Sleep up to ms (at least 1) for events, waking for an owed present. */
static void sleep_for(frontend *f, Uint64 now, Uint64 ms)
{
    ms = present_due(f, now, ms);
    pump(f, (Uint32)(ms > 0 ? ms : 1));
}

/* Type the script's next key. Returns false when there is none: the
 * script ended (or failed), or it was \t, the time running out, which
 * *timeout says. */
static bool type_script(frontend *f, bool *timeout)
{
    *timeout = false;
    int code = cok_key_script_read(&f->script);
    if (code < 0) {
        *timeout = code == COK_KEY_TIMEOUT;
        return false;
    }
    int scan = code == 0 ? cok_key_script_read(&f->script) : -1;
    if (!press(f, code, scan, false, false)) return false;
    pump(f, 0);
    return true;
}

static bool same(const cok_keystroke *a, const cok_keystroke *b)
{
    return a->code == b->code && a->scan == b->scan;
}

/* A group of keys typed ahead, if the script has one due (now: at a key
 * read), pressed all at once into the buffer; a key repeated in a row is
 * held down, its repeats coming as the keyboard's typematic sends them. */
static void type_ahead(frontend *f, bool now)
{
    if (!f->scripted || f->script_error[0] != '\0') return;
    cok_keystroke strokes[COK_KEYS_BUFFERED];
    size_t n = cok_key_script_ahead(&f->script, now, strokes, KEYS_MAX - f->count);
    for (size_t i = 0; i < n; ++i) {
        bool repeat = i > 0 && same(&strokes[i], &strokes[i - 1]);
        bool hold = i + 1 < n && same(&strokes[i], &strokes[i + 1]);
        if (!press(f, strokes[i].code, strokes[i].scan, repeat, hold)) return;
    }
    if (n > 0) pump(f, 0);
}

/* The game's hooks. */

static void timing_off(frontend *f)
{
    if (!f->game->timed) f->timing = false;
}

/* Crt.ReadKey (1962:030f), with what 67b5:03e2 does while it waits: the
 * timed menu's timeout and the small picture's animation. */
static int read_key(void *context)
{
    frontend *f = context;
    cok_adventure *game = f->game;
    if (f->owed >= 0) {
        int scan = f->owed;
        f->owed = -1;
        return scan;
    }
    shoot(f);
    timing_off(f);
    if (f->count == 0) type_ahead(f, true);
    Uint64 now = SDL_GetTicks64();
    if (game->timed && !f->timing) {
        f->timing = true;
        f->timed_from = now;
    }
    f->frame_from = now;
    bool forced = false;
    for (;;) {
        pump(f, 0);
        if (f->closing || f->shot_failed) return -1;
        if (f->count > 0) {
            int *key = f->queue[f->head];
            f->head = (f->head + 1) % KEYS_MAX;
            --f->count;
            f->owed = key[1];
            return key[0];
        }
        now = SDL_GetTicks64();
        unsigned long pause;
        if (f->scripted && f->pause_until == 0 && cok_key_script_pause(&f->script, &pause))
            f->pause_until = now + pause * 10;
        if (f->scripted && f->pause_until <= now) {
            f->pause_until = 0;
            bool timeout;
            if (type_script(f, &timeout)) continue;
            if (timeout) return COK_KEY_TIMEOUT;
            if (f->script_error[0] != '\0' || !f->keep_playing) return -1;
            f->scripted = false;
        }
        Uint64 wait = f->pause_until > now ? f->pause_until - now : 1000;
        if (wait > 1000) wait = 1000;
        if (game->timed && f->timing) {
            /* 67b5:0579: DS:6e11 hundredths since the menu began. */
            Uint64 limit = (Uint64)game->timeout * 10u;
            if (now - f->timed_from >= limit) {
                f->timing = false;
                return COK_KEY_TIMEOUT;
            }
            if (limit - (now - f->timed_from) < wait) wait = limit - (now - f->timed_from);
        }
        uint32_t frame_ms;
        if (cok_adventure_menu_animates(game, &frame_ms)) {
            /* 67b5:0502: the next frame once this one's time is up. */
            if (now - f->frame_from >= frame_ms) {
                cok_adventure_menu_frame(game);
                f->frame_from = now;
                cok_adventure_menu_animates(game, &frame_ms); /* the new frame's */
            }
            if (frame_ms - (now - f->frame_from) < wait) wait = frame_ms - (now - f->frame_from);
        }
        present(f, !forced, NULL);
        forced = true;
        sleep_for(f, now, wait);
    }
}

/* 1614:045c: the keys waiting are lost, logged as eclplay logs those
 * typed ahead. */
static void flush_keys(cok_adventure *game, void *context)
{
    (void)game;
    frontend *f = context;
    size_t dropped = f->count + (f->owed >= 0);
    if (f->log && dropped > 0) fprintf(stderr, "flush: %zu keys typed ahead dropped\n", dropped);
    f->count = 0;
    f->owed = -1;
}

/* KeyPressed (1614:03c2), which the title, resting and the computer's
 * turns poll; in a script, \k presses the key after it now, and keys
 * typed ahead may arrive. */
static bool key_pending(cok_adventure *game, void *context)
{
    frontend *f = context;
    pump(f, 0);
    timing_off(f);
    cok_key_script_tick(&f->script);
    type_ahead(f, false);
    if (game->resting) { /* even with --no-delay: not a wait of the game's */
        /* 57e4's rest loop polls once a step and waits for nothing, so the
         * original rested at its machine's pace, showing the time left
         * every five steps (57e4:0764); each step takes a hundredth here,
         * so that the count shows and a key can stop it. */
        Uint64 until = f->rest_from + REST_MS;
        for (;;) {
            present(f, false, NULL);
            Uint64 now = SDL_GetTicks64();
            if (f->closing || f->count > 0 || now >= until) break;
            sleep_for(f, now, until - now);
        }
        f->rest_from = SDL_GetTicks64();
    } else {
        present(f, false, NULL);
    }
    /* Closed: the key read next ends the run. */
    if (f->closing || f->count > 0 || f->owed >= 0) return true;
    if (f->scripted && cok_key_script_poll(&f->script)) {
        bool timeout;
        type_script(f, &timeout);
        return true;
    }
    return false;
}

/* Crt.Delay: sleep, showing the screen as it changes; keys typed meanwhile
 * wait in the buffer. A closed window ends the run at once. */
static void delay(cok_adventure *game, unsigned ms, void *context)
{
    frontend *f = context;
    timing_off(f);
    cok_key_script_tick(&f->script);
    type_ahead(f, false);
    Uint64 until = SDL_GetTicks64() + (f->no_delay ? 0 : ms);
    for (;;) {
        pump(f, 0);
        if (f->closing) {
            game->input_ended = true;
            game->vm.abort = true;
            return;
        }
        present(f, false, NULL);
        Uint64 now = SDL_GetTicks64();
        if (now >= until) break;
        sleep_for(f, now, until - now);
    }
    /* With --log, a check that the window keeps up: what the game drew
     * before it waits must be shown within a frame or two of waiting. */
    const cok_picture *screen = &game->screen;
    if (f->log && !f->no_delay && screen->frame_size == SCREEN_BYTES &&
        memcmp(f->on_window, screen->pixels, SCREEN_BYTES) != 0) {
        Uint64 now = SDL_GetTicks64();
        if (f->owed_from == 0) {
            f->owed_from = now;
        } else if (now - f->owed_from >= 10 * PRESENT_MS) {
            fprintf(stderr, "stale: the window showed an old screen for %llu ms of delays\n",
                    (unsigned long long)(now - f->owed_from));
            f->owed_from = now;
        }
    } else {
        f->owed_from = 0;
    }
}

static void battlefield(cok_adventure *game, void *context)
{
    (void)game;
    shoot(context);
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    frontend *f = context;
    if (f->log) fprintf(stderr, "%s: %s\n", kind, text);
}

static void unported(cok_adventure *game, void *context)
{
    frontend *f = context;
    if (!f->log) return;
    const char *name = cok_ecl_opcode_name(game->vm.opcode);
    fprintf(stderr, "[%s", name != NULL ? name : "?");
    for (size_t i = 0; i < game->vm.operands; ++i)
        fprintf(stderr, " %u", cok_ecl_value(&game->vm, i));
    fprintf(stderr, "]\n");
}

/* Where the port stops: the reason on the screen's last rows, over a black
 * band, until a key or the window closes. */
static void show_stop(frontend *f, const char *reason)
{
    cok_adventure *game = f->game;
    if (game->font.count == 0 || game->screen.pixels == NULL) {
        /* Nothing to write it with: a message box. */
        if (!f->scripted)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Champions of Krynn", reason, f->window);
        return;
    }
    char lines[12][41];
    size_t n = 0;
    char text[700];
    snprintf(text, sizeof text, "The port stops here: %s", reason);
    const char *p = text;
    while (*p != '\0' && n < 10) {
        size_t len = strlen(p);
        size_t cut = len <= 40 ? len : 40;
        if (len > 40) {
            size_t space = cut;
            while (space > 0 && p[space] != ' ') --space;
            if (space > 0) cut = space;
        }
        snprintf(lines[n++], sizeof lines[0], "%.*s", (int)cut, p);
        p += cut;
        while (*p == ' ') ++p;
    }
    snprintf(lines[n++], sizeof lines[0], "Press a key to quit.");
    int top = 25 - (int)n;
    cok_picture_fill(&game->screen, 0, top * 8 - 2, 40, (size_t)(200 - (top * 8 - 2)), 0);
    for (size_t i = 0; i < n; ++i)
        cok_text_string(&game->screen, &game->font, lines[i], 0, top + (int)i,
                        i + 1 == n ? 15 : 12, 0);
    f->count = 0;
    f->owed = -1;
    present(f, true, NULL);
    if (f->scripted) {
        /* A script has no one to read it: show it to --shots and go. */
        shoot(f);
        return;
    }
    while (!f->closing && f->count == 0) {
        pump(f, 1000);
        present(f, false, NULL);
    }
}

static bool number(const char *text, unsigned long max, unsigned long *value)
{
    char *end;
    errno = 0;
    *value = strtoul(text, &end, 10);
    return errno == 0 && end != text && *end == '\0' && *value <= max;
}

/* Turbo Pascal's Randomize (1a46:1200, which startup calls at 3e99:005b):
 * RandSeed from DOS's time, CX hours and minutes, DX seconds and
 * hundredths. */
static uint32_t randomize(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    uint32_t cx = (uint32_t)tm.tm_hour << 8 | (uint32_t)tm.tm_min;
    uint32_t dx = (uint32_t)tm.tm_sec << 8 | (uint32_t)(ts.tv_nsec / 10000000);
    return cx | dx << 16;
}

int main(int argc, char **argv)
{
    static frontend f;
    static cok_adventure game;
    f.game = &game;
    f.owed = -1;
    f.crt = true;
    f.screenshots = "screenshots";
    const char *assets = "Assets", *saves = "saves", *keys = NULL;
    unsigned long scale = 3, seed = 0;
    bool seeded = false, woof = false, helm = false, fullscreen = false;
    for (int i = 1; i < argc; ++i) {
        const char *option = argv[i];
        bool has_value = i + 1 < argc;
        if (strcmp(option, "--assets") == 0 && has_value) {
            assets = argv[++i];
        } else if (strcmp(option, "--saves") == 0 && has_value) {
            saves = argv[++i];
        } else if (strcmp(option, "--scale") == 0 && has_value &&
                   number(argv[i + 1], 16, &scale) && scale >= 1) {
            ++i;
        } else if (strcmp(option, "--aspect") == 0 && has_value &&
                   (strcmp(argv[i + 1], "4:3") == 0 || strcmp(argv[i + 1], "1:1") == 0)) {
            f.crt = strcmp(argv[++i], "4:3") == 0;
        } else if (strcmp(option, "--fullscreen") == 0) {
            fullscreen = true;
        } else if (strcmp(option, "--screenshots") == 0 && has_value) {
            f.screenshots = argv[++i];
        } else if (strcmp(option, "--seed") == 0 && has_value &&
                   number(argv[i + 1], 0xffffffffUL, &seed)) {
            seeded = true;
            ++i;
        } else if (strcmp(option, "--woof") == 0) {
            woof = true;
        } else if (strcmp(option, "--helm") == 0) {
            helm = true;
        } else if (strcmp(option, "--log") == 0) {
            f.log = true;
        } else if (strcmp(option, "--keys") == 0 && has_value) {
            keys = argv[++i];
        } else if (strcmp(option, "--continue") == 0) {
            f.keep_playing = true;
        } else if (strcmp(option, "--no-delay") == 0) {
            f.no_delay = true;
        } else if (strcmp(option, "--shots") == 0 && has_value) {
            f.shots = argv[++i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (strlen(saves) >= sizeof game.save_dir) {
        fprintf(stderr, "%s: directory name too long\n", saves);
        return 1;
    }
    if (mkdir(saves, 0755) != 0 && errno != EEXIST) {
        perror(saves);
        return 1;
    }
    if (f.shots != NULL && f.crt && scale < 2) {
        fprintf(stderr, "--shots: at --scale 1 the 4:3 window blends the screen's rows; use "
                        "--scale 2 or more, or --aspect 1:1\n");
        return 2;
    }
    if (f.shots != NULL && mkdir(f.shots, 0755) != 0 && errno != EEXIST) {
        perror(f.shots);
        return 1;
    }
    f.scripted = keys != NULL;
    f.script = cok_key_script_start(keys);

    /* The palette, as cok_image_rgb gives the 16 colours. */
    uint8_t ramp[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    cok_image colours = {.width = 16, .height = 1, .pixels = ramp};
    cok_image_rgb(&colours, &f.palette[0][0]);

    /* SDL turns SIGINT and SIGTERM into a quit only where they are not
     * ignored, as a shell ignores SIGINT for a job it starts in the
     * background: take them back. */
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int height = f.crt ? 240 : SCREEN_H;
    f.window = SDL_CreateWindow("Champions of Krynn", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, SCREEN_W * (int)scale, height * (int)scale,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                    (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    f.fullscreen = fullscreen;
    if (f.window != NULL) f.renderer = SDL_CreateRenderer(f.window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (f.window != NULL && f.renderer == NULL) f.renderer = SDL_CreateRenderer(f.window, -1, 0);
    if (f.renderer != NULL) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        f.texture = SDL_CreateTexture(f.renderer, SDL_PIXELFORMAT_RGB24,
                                      SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    }
    if (f.texture == NULL) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(f.texture, SDL_ScaleModeNearest);
    SDL_SetWindowMinimumSize(f.window, SCREEN_W, height);
    SDL_StopTextInput(); /* Keys come as key events; no input method's window. */
    if (f.log) {
        int w, h;
        SDL_GetWindowSize(f.window, &w, &h);
        fprintf(stderr, "window: %dx%d\n", w, h);
    }

    cok_keyboard keyboard = {read_key, &f, NULL};
    cok_adventure_hooks hooks = {.unported = unported, .log = log_line, .delay = delay,
                                 .key_pending = key_pending, .flush = flush_keys,
                                 .battlefield = battlefield, .context = &f};
    int result = 0;
    if (!cok_adventure_open(&game, assets, &keyboard, &hooks)) {
        fprintf(stderr, "%s\n", game.error);
        show_stop(&f, game.error);
        result = 1;
    } else {
        snprintf(game.save_dir, sizeof game.save_dir, "%s", saves);
        game.combat_stub = COK_COMBAT_PLAYER; /* The player's turns read the keys. */
        game.helm = helm;
        game.vm.seed = seeded ? (uint32_t)seed : randomize();
        cok_ecl_status status = cok_start_game(&game, woof);
        if (f.shot_failed) {
            result = 1;
        } else if (f.script_error[0] != '\0') {
            fprintf(stderr, "--keys: %s\n", f.script_error);
            result = 2;
        } else if (f.closing) {
            /* Closed: nothing more to do. */
        } else if (status != COK_ECL_OK) {
            const char *why = game.error[0] != '\0' ? game.error : cok_ecl_status_string(status);
            if (game.vm.ip != 0)
                fprintf(stderr, "stopped in block %u at %04x: %s\n", game.vm.block, game.vm.ip, why);
            else
                fprintf(stderr, "stopped: %s\n", why);
            show_stop(&f, why);
            result = 1;
        } else if (game.quit) {
            printf("(quit to DOS)\n");
        } else if (game.title) {
            printf("(out of keys at the title)\n");
        } else if (game.vm.mode == 0) {
            printf("(out of keys in the start menu)\n");
        } else {
            printf("(out of keys in block %u at %04x)\n", game.vm.block, game.vm.ip);
        }
    }
    cok_adventure_close(&game);
    if (f.upscaled != NULL) SDL_DestroyTexture(f.upscaled);
    SDL_DestroyTexture(f.texture);
    SDL_DestroyRenderer(f.renderer);
    SDL_DestroyWindow(f.window);
    SDL_Quit();
    return result;
}
