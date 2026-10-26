/* Run an ECL block on the adventure screen without a display: keys come
 * from the command line, text and menus go to standard output, and the
 * screen can be saved as a BMP whenever the game waits for a key. */
#include "adventure.h"
#include "arena.h"
#include "config.h"
#include "crt.h"
#include "keyscript.h"
#include "setup.h"
#include "sound.h"
#include "start.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    cok_key_script keys;
    /* Keys typed ahead (\[...\]), waiting in the keyboard's buffer, and the
     * scan code owed after one of them read as 0, or -1. */
    cok_keystroke buffer[COK_KEYS_BUFFERED];
    size_t buffered;
    int owed;
    const char *shots;    /* Directory for BMPs, or NULL. */
    unsigned shot;
    const char *map;      /* File for each battle's map, or NULL. */
    bool trace;
    cok_adventure *game;
    /* --wav: the sound rendered as the game's time passes (its delays and
     * the script's \pN. pauses; key reads take none), to a WAV file. */
    cok_sound *sound;
    FILE *wav;
    const char *wav_path;
    uint64_t ms, frames;
    bool calls; /* --sound-calls */
    /* The text screen while startup shows it (11c7:031a), or NULL. */
    cok_crt *crt;
    /* The machine's mouse driver and joystick (--mouse, --joystick), which
     * the keys' \m and \j move: the stick's counts and buttons; a click's
     * buttons, which come up once the game has read it (tapped). */
    bool mouse, stick;
    uint16_t stick_x, stick_y;
    uint8_t stick_buttons;
    bool tap, tapped;
    bool fresh;           /* moved in a wait of the game's: its next read may take it */
    bool ended;           /* The keys ran out where the game polls them. */
    cok_game_port port;
} player;

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [options] ASSETS BLOCK\n"
            "       %s [options] --load SAVE ASSETS [BLOCK]\n"
            "       %s [options] --start-menu ASSETS\n"
            "Run ECL block BLOCK (as the game does on entering it) from the DAX files in\n"
            "ASSETS, printing its text, menus and choices.\n"
            "  --game NAME     the game whose files ASSETS holds: champions (the default)\n"
            "  --keys KEYS     keys to type; \\r Enter, \\e Escape, \\b Backspace,\n"
            "                  \\< \\> \\^ \\v the arrows, \\@X Alt and letter X, \\#HH the\n"
            "                  scan code HH (hex), \\\\ a backslash; \\k presses the next\n"
            "                  key while the party rests or the title waits; \\t lets\n"
            "                  the title's menu time out; \\[KEYS\\] types KEYS ahead,\n"
            "                  at once at the game's next wait (a delay or a poll) or key\n"
            "                  read, into the buffer, where a flush drops them (logged as\n"
            "                  flush:); \\wN. holds what follows back for N of the game's\n"
            "                  waits; \\mX,Y,B. clicks the mouse's buttons B at pixel X, Y\n"
            "                  (B left out: a move), \\jX,Y,B. puts the stick at counts X, Y\n"
            "                  with buttons B. In a battle the player's turns read them\n"
            "                  (3995:0573) unless --combat says otherwise\n"
            "  --start-menu    play the game from startup: the title, its menu, the start\n"
            "                  menu and the adventure, game after game\n"
            "  --woof          play as if started with Woof: Ctrl-D, Ctrl-Z and Ctrl-C\n"
            "                  work (the debug mode, DEBUG.TXT in --saves DIR); with\n"
            "                  --start-menu, start without the title\n"
            "  --play          then take adventure commands until the keys run out\n"
            "  --test-party N  add N (1-8) characters made up for testing, without saved\n"
            "                  games: fighters, clerics, mages and thieves of level 1\n"
            "  --party SAVE    add the characters of saved game SAVE (SAVGAMA.DAT) to the\n"
            "                  party, from the files beside it\n"
            "  --load SAVE     load saved game SAVE first; without BLOCK resume it where\n"
            "                  it was saved, only that block's load vector run at first\n"
            "                  (with BLOCK, the block is entered at once, all vectors)\n"
            "  --saves DIR     write saved games (camp's Save) to DIR; KRYNN.CFG there\n"
            "                  comes before the assets' and is where the setup writes\n"
            "  --import DIR    where a game that imports another's characters finds\n"
            "                  them (Death Knights' Champions party; default --saves)\n"
            "  --config FILE   KRYNN.CFG: read FILE, or with --start-menu write it there\n"
            "                  from the setup if it is missing\n"
            "  --combat HOW    without it, COMBAT's rounds have the computer play its\n"
            "                  turns (the monsters') while the player's read the keys,\n"
            "                  or with no --keys pass, which the party seldom survives;\n"
            "                  HOW decides the battle in place of its rounds: won (every\n"
            "                  monster against the party drops), fled (the party flees)\n"
            "                  or lost (the party dies); or play the rounds with gods,\n"
            "                  the original's Helm cheat (Alt-X) at each player's turn;\n"
            "                  melee, every turn attacking the nearest enemy it can\n"
            "                  reach; auto, the player's first turn pressing Alt-Q, which\n"
            "                  puts the whole party on Auto; or pass, every turn\n"
            "                  passing, the computer's too\n"
            "  --helm          play as if started with Helm, which lifts Area's Not Here\n"
            "                  and enables the Gods cheat (implied by --combat gods)\n"
            "  --combat-map FILE  write each battle's map and combatants to FILE as text\n"
            "  --seed N        start Turbo Pascal's Random from N (default 0)\n"
            "  --shots DIR     save DIR/NNN.bmp each time the game waits for a key, and\n"
            "                  when a battle has been set up\n"
            "  --screen FILE   save the final screen as FILE (BMP)\n"
            "  --file N        ECL file 1-3 (default: the first holding BLOCK)\n"
            "  --vector N      run only vector N (0-4) instead of entering the block\n"
            "  --start ADDR    run only from code address ADDR (hex)\n"
            "  --at X,Y,DIR    party position and facing (0, 2, 4 or 6)\n",
            program, program, program);
    fputs("  --character FILE  add the character in FILE (CHRDATA1.SAV, BOB.WHO; Death\n"
          "                  Knights' CHRDATA1.GAM, BOB.PC) to the party, with its other\n"
          "                  files beside it; up to 8, after --party's\n"
          "  --set ADDR=VAL  set a variable before running, in hex (repeatable)\n"
          "  --effect M,ID,MINUTES,VALUE  give party member M (1 the first) effect ID\n"
          "                  for MINUTES (0 for good) with VALUE, its handler run when\n"
          "                  it is removed, in hex, as a saved game's .SFX holds it\n"
          "                  (repeatable)\n",
          stderr);
    fputs("  --still         load only the first frame of each picture\n"
          "  --wav FILE      render the sound to FILE (16-bit mono WAV, 44,100 Hz) as\n"
          "                  the game's time passes: its delays and \\pN. in the keys\n"
          "                  (N hundredths of a second); key reads take none; a second\n"
          "                  more at the end\n"
          "  --display MODE  ega, tandy or cga, in place of KRYNN.CFG's (its first\n"
          "                  line; with --start-menu the setup asks without one, else\n"
          "                  the Tandy); --shots and --screen\n"
          "                  show CGA in its four colours\n"
          "  --sound DEVICE  with --wav, the sound device: tandy, adlib, speaker or off,\n"
          "                  in place of KRYNN.CFG's (the setup's, or the Tandy without\n"
          "                  one); without the title (a block, --load) the driver is\n"
          "                  installed first, as main does\n"
          "  --sound-calls   print the game's calls to the sound driver as call: lines\n"
          "                  (17e8's commands, its installs, the overlays' uninstalls,\n"
          "                  DAX reads, Ctrl-S, the title's music, quitting)\n"
          "  --trace         print each instruction's address and name\n"
          "  --mouse         the machine has a mouse driver, which \\m in the keys\n"
          "                  moves and clicks\n"
          "  --joystick      the machine has a joystick, which \\j in the keys moves\n"
          "  --input DEVICE  mouse, joystick or none, in place of KRYNN.CFG's (its\n"
          "                  third line: M, J, N)\n",
          stderr);
}

static void save(player *p, const char *path)
{
    size_t at;
    if (!cok_picture_cga_valid(&p->game->screen, &at)) {
        /* A port bug: a picture drawn on CGA mode's screen without its
         * colours. Stop rather than show it as one of the four. */
        fflush(stdout);
        fprintf(stderr, "%s: the screen holds a value above 3 at %zu,%zu in CGA mode\n", path,
                at % 320, at / 320);
        exit(1);
    }
    cok_image image = cok_picture_frame(&p->game->screen, 0);
    /* The mouse's cursor where its driver draws it. */
    static uint8_t pixels[320 * 200 / 2];
    if (image.width == 320 && image.height == 200) {
        memcpy(pixels, image.pixels, sizeof pixels);
        cok_mouse_draw(&p->game->mouse.driver, pixels, p->game->display == COK_DISPLAY_CGA);
        image.pixels = pixels;
    }
    cok_image_status status = cok_image_bmp(&image, path);
    if (status != COK_IMAGE_OK)
        fprintf(stderr, "%s: %s\n", path, cok_image_status_string(status));
}

/* The text screen, 640x200, as a BMP. */
static void save_crt(player *p, const char *path)
{
    static uint8_t pixels[COK_CRT_WIDTH * COK_CRT_HEIGHT / 2];
    cok_crt_render(p->crt, pixels);
    cok_image image = {.width = COK_CRT_WIDTH, .height = COK_CRT_HEIGHT, .pixels = pixels};
    cok_image_status status = cok_image_bmp(&image, path);
    if (status != COK_IMAGE_OK)
        fprintf(stderr, "%s: %s\n", path, cok_image_status_string(status));
}

/* The sound's time moves on by ms, the samples due written. */
static void advance(player *p, uint64_t ms)
{
    if (p->sound == NULL) return;
    p->ms += ms;
    uint64_t due = p->ms * COK_SOUND_RATE / 1000;
    int16_t buffer[4096];
    uint8_t bytes[sizeof buffer];
    while (p->frames < due) {
        size_t n = due - p->frames < 4096 ? (size_t)(due - p->frames) : 4096;
        cok_sound_render(p->sound, buffer, n);
        for (size_t k = 0; k < n; ++k) {
            bytes[2 * k] = (uint8_t)buffer[k];
            bytes[2 * k + 1] = (uint8_t)((uint16_t)buffer[k] >> 8);
        }
        if (p->wav != NULL) fwrite(bytes, 2, n, p->wav);
        p->frames += n;
    }
}

static void put32(uint8_t *at, uint32_t v)
{
    for (int k = 0; k < 4; ++k) at[k] = (uint8_t)(v >> (8 * k));
}

/* A WAV header for frames 16-bit mono samples at COK_SOUND_RATE. */
static void wav_header(uint8_t header[44], uint64_t frames)
{
    uint32_t data = (uint32_t)(frames * 2);
    memcpy(header, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0", 24);
    put32(header + 4, 36 + data);
    put32(header + 24, COK_SOUND_RATE);
    put32(header + 28, COK_SOUND_RATE * 2);
    memcpy(header + 32, "\x02\0\x10\0data", 8);
    put32(header + 40, data);
}

/* The game's call to the sound driver. */
static void sound(cok_adventure *game, cok_sound_call call, unsigned value, void *context)
{
    player *p = context;
    if (p->calls) {
        static const char *const names[] = {"command", "install", "overlay", "overlay end",
                                            "record", "ctrl-s", "music", "music end", "quit",
                                            "ctrl-u"};
        if (call == COK_SOUND_COMMAND || (call == COK_SOUND_MUSIC && value != 0))
            printf("call: %s %u\n", names[call], value);
        else
            printf("call: %s\n", names[call]);
    }
    if (p->sound == NULL) return;
    cok_sound_call_at(p->sound, call, value);
    char text[100];
    if (call == COK_SOUND_MUSIC &&
        cok_sound_music_unported(p->sound, cok_game_rules(game), text, sizeof text))
        cok_adventure_log(game, "unported", text);
}

static unsigned clock_speed(cok_adventure *game, void *context)
{
    (void)game;
    player *p = context;
    return p->sound != NULL ? cok_sound_clock_speed(p->sound) : 1;
}

/* The run is over: the tail rendered, the header written. */
static int finish_wav(player *p)
{
    if (p->sound == NULL) return 0;
    advance(p, COK_SOUND_TAIL_MS);
    int result = 0;
    if (p->wav != NULL) {
        uint8_t header[44];
        wav_header(header, p->frames);
        if (fseek(p->wav, 0, SEEK_SET) != 0 || fwrite(header, 1, sizeof header, p->wav) != 44 ||
            fclose(p->wav) != 0) {
            perror(p->wav_path);
            result = 1;
        }
        p->wav = NULL;
    }
    if (p->sound->divide_error)
        fprintf(stderr, "sound: the music driver divided by too little (runtime error 200)\n");
    cok_sound_free(p->sound);
    free(p->sound);
    p->sound = NULL;
    return result;
}

/* Put a group of keys typed ahead in the buffer, if the script has one
 * due (now: at a key read). */
static void type_ahead(player *p, bool now)
{
    p->buffered += cok_key_script_ahead(&p->keys, now, p->buffer + p->buffered,
                                        COK_KEYS_BUFFERED - p->buffered);
}

/* A click's buttons up, once the game has read it. */
static void release(player *p)
{
    if (!p->tapped) return;
    p->tap = p->tapped = false;
    if (p->game != NULL) p->game->mouse.driver.buttons = 0;
}

/* \m or \j: the pointer to the pixel given, its buttons down; the stick's
 * counts and buttons. */
static void apply(player *p, const cok_device_event *e)
{
    if (e->kind == 'm' && p->game != NULL && p->mouse) {
        cok_mouse_driver *driver = &p->game->mouse.driver;
        driver->x = e->x > 319 ? 638 : e->x * 2;
        driver->y = e->y > 199 ? 199 : e->y;
        driver->buttons = e->buttons;
        p->tap = e->buttons != 0;
    } else if (e->kind == 'j' && p->stick) {
        p->stick_x = (uint16_t)e->x;
        p->stick_y = (uint16_t)e->y;
        p->stick_buttons = (uint8_t)e->buttons;
    }
}

/* The game port: the stick's counts, as \j left them. */
static bool stick_read(void *context, unsigned stick, uint16_t *x, uint16_t *y,
                       uint8_t *buttons)
{
    player *p = context;
    if (!p->stick || stick != 1) return false;
    *x = p->stick_x;
    *y = p->stick_y;
    *buttons = p->stick_buttons;
    return true;
}

/* The game polls the mouse or the stick in a loop: a click's buttons come
 * up, and a \m or \j next in the keys comes; the keys' end ends the run. */
static void stick_wait(void *context, unsigned ms)
{
    (void)ms;
    player *p = context;
    bool released = p->tapped;
    release(p);
    if (cok_key_script_device(&p->keys)) {
        apply(p, &p->keys.event);
        p->fresh = true;
    } else if (p->stick_buttons != 0) {
        p->stick_buttons = 0; /* let go, unless the keys say otherwise */
    } else if (!released && cok_key_script_done(&p->keys)) {
        p->ended = true;
        if (p->game != NULL) {
            p->game->input_ended = true;
            p->game->vm.abort = true;
        }
    }
}

static void device_wait(cok_adventure *game, unsigned ms, void *context)
{
    (void)game;
    stick_wait(context, ms);
}

static int next_key(void *context)
{
    player *p = context;
    if (p->owed >= 0) {
        int scan = p->owed;
        p->owed = -1;
        return scan;
    }
    release(p);
    if (!cok_key_script_owed(&p->keys) && p->shots != NULL) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%03u.bmp", p->shots, p->shot++);
        if (p->crt != NULL)
            save_crt(p, path);
        else
            save(p, path);
    }
    if (!cok_key_script_owed(&p->keys) && p->buffered == 0) type_ahead(p, true);
    if (p->buffered > 0 && !cok_key_script_owed(&p->keys)) {
        cok_keystroke k = p->buffer[0];
        memmove(p->buffer, p->buffer + 1, --p->buffered * sizeof *p->buffer);
        p->owed = k.scan;
        return k.code;
    }
    unsigned long pause;
    for (;;) {
        bool device = p->fresh;
        p->fresh = false;
        /* \m, \j: given to the game when it waits on them and they give a
         * key; else on to the next key. */
        if (device && p->game != NULL && p->game->device_wait != COK_WAIT_KEYS &&
            cok_adventure_device_ready(p->game)) {
            p->tapped = p->tap;
            return COK_KEY_DEVICE;
        }
        while (cok_key_script_pause(&p->keys, &pause)) advance(p, pause * 10u);
        int key = cok_key_script_read(&p->keys);
        if (key != COK_KEY_DEVICE) return key;
        apply(p, &p->keys.event);
        p->fresh = true;
    }
}

/* KeyPressed: a key typed ahead waits, or \k in the keys (which the rest
 * loop polls). */
static bool key_pending(cok_adventure *game, void *context)
{
    (void)game;
    player *p = context;
    cok_key_script_tick(&p->keys);
    type_ahead(p, false);
    if (p->buffered > 0 || p->owed >= 0) return true;
    return cok_key_script_poll(&p->keys);
}

/* Crt.Delay: no waiting here, but keys typed ahead may arrive, and the
 * sound's time moves on. */
static void delay(cok_adventure *game, unsigned ms, void *context)
{
    (void)game;
    player *p = context;
    advance(p, ms);
    cok_key_script_tick(&p->keys);
    type_ahead(p, false);
}

/* 1614:045c: the keys typed ahead are lost (the script's own, typed as the
 * game reads them, are not), and logged as "flush". */
static void flush(cok_adventure *game, void *context)
{
    (void)game;
    player *p = context;
    if (p->buffered == 0 && p->owed < 0) return;
    char text[64];
    snprintf(text, sizeof text, "%zu keys typed ahead dropped", p->buffered + (p->owed >= 0));
    printf("flush: %s\n", text);
    p->buffered = 0;
    p->owed = -1;
}

/* Startup's Crt.Delay: keys typed ahead may arrive; the sound's time
 * starts with the game's, after the text screens. */
static void crt_delay(unsigned ms, void *context)
{
    (void)ms;
    player *p = context;
    cok_key_script_tick(&p->keys);
    type_ahead(p, false);
}

/* A row of the text screen, as the cursor leaves it. */
static void crt_log(const char *row, void *context)
{
    (void)context;
    printf("crt: %s\n", row);
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    (void)context;
    printf("%s: %s\n", kind, text);
}

/* A combatant's mark on the map: 1-9, then a-z and A-Z, else *. */
static char mark(unsigned n)
{
    static const char marks[] = "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    return n - 1 < sizeof marks - 1 ? marks[n - 1] : '*';
}

/* Append the battle's map to the file of --combat-map: a picture of it,
 * each cell a combatant's mark, or for its terrain . for plain floor, #
 * where none can walk, T a table, h a chair, _ a body, : rough ground
 * (cost 2), ~ water (cost 4) and , anything else; then each cell's
 * value in hex; then the combatants. */
static void battlefield(cok_adventure *game, void *context)
{
    player *p = context;
    if (p->shots != NULL) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%03u.bmp", p->shots, p->shot++);
        save(p, path);
    }
    if (p->map == NULL) return;
    FILE *f = fopen(p->map, "a");
    if (f == NULL) {
        perror(p->map);
        return;
    }
    const cok_combat *c = &game->combat;
    const cok_rules *rules = cok_game_rules(game);
    fprintf(f, "battle in block %u: view %d,%d, %u combatants\n", game->vm.block, c->view_x,
            c->view_y, c->count - 1u);
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y) {
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) {
            uint8_t v = c->cells[y][x], who = c->occupant[y][x];
            const cok_terrain t = cok_combat_terrain(
                rules, v < cok_data_count(rules->data, COK_DATA_TERRAIN, 0) ? v : 0);
            char ch = who != 0 ? mark(who) : v == COK_COMBAT_FLOOR || v == 0x36 ? '.'
                    : t.cost == 0xff ? '#' : v == 0x1a ? 'T' : v == 0x1b ? 'h'
                    : v == COK_COMBAT_BODY ? '_' : t.cost == 2 ? ':' : t.cost == 4 ? '~' : ',';
            fputc(ch, f);
        }
        fputc('\n', f);
    }
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y) {
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) fprintf(f, "%02x", c->cells[y][x]);
        fputc('\n', f);
    }
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (unsigned n = 1; n < c->count; ++n) {
        const cok_combatant *e = &c->combatant[n];
        const uint8_t *r = e->character->record;
        uint8_t length = cok_field_u8_at(L, r, COK_FIELD_NAME, 0);
        fprintf(f, "%c %u %.*s: at %d,%d size %u side %u facing %u\n", mark(n), n,
                length > 15 ? 15 : length,
                (const char *)r + cok_field_offset_at(L, COK_FIELD_NAME, 1), e->x, e->y, e->size,
                cok_field_u8(L, r, COK_FIELD_HOSTILE), e->character->combat->facing);
    }
    for (unsigned k = 0; k < c->bodies; ++k)
        fprintf(f, "body at %d,%d on %02x\n", c->body[k].x, c->body[k].y, c->body[k].cell);
    fputc('\n', f);
    fclose(f);
}

static void unported(cok_adventure *game, void *context)
{
    (void)context;
    const char *name = cok_ecl_opcode_name(game->vm.rules, game->vm.opcode);
    printf("[%s", name != NULL ? name : "?");
    for (size_t i = 0; i < game->vm.operands; ++i) printf(" %u", cok_ecl_value(&game->vm, i));
    printf("]\n");
}

static void trace(cok_adventure *game, void *context)
{
    player *p = context;
    if (!p->trace) return;
    const cok_ecl *vm = &game->vm;
    uint8_t opcode = vm->code[vm->ip - COK_ECL_BASE];
    const char *name = cok_ecl_opcode_name(vm->rules, opcode);
    printf("  %04x %s\n", vm->ip, name != NULL ? name : "?");
}

/* An item of type with name part `part`, weight and readied. */
static void give_item(cok_character *c, uint8_t type, uint8_t part, uint8_t armour, uint16_t weight)
{
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[0x2e] = type;
    item[0x31] = part;
    item[0x30] = armour;
    item[0x34] = 1;
    item[0x37] = (uint8_t)weight;
    item[0x38] = (uint8_t)(weight >> 8);
    cok_character_insert_item(c, c->item_count, item);
}

/* Add count characters made up for testing, as a saved game's would be
 * (4b6d:1b34): a fighter, a cleric of Mishakal, a White mage and a thief
 * in turn (the eighth a mage), human, of level 1, with their weapons and
 * armour readied, a little steel, the cleric's Cure Light Wounds and the
 * mage's Detect Magic memorized (the seventh also Charm Monsters, with a
 * wand; the eighth Stinking Cloud instead); their stats
 * computed as a character's are when it loads (6346:0d20, 66c2:0433),
 * each counted in 0x7f3e and the first selected. */
static bool test_party(cok_adventure *game, unsigned long count)
{
    static const char *const names[8] = {"ALDA", "BRAM", "CERA", "DUNN",
                                         "ELIN", "FARO", "GWEN", "HOLT"};
    /* Class (+0x5b), the level's byte (+0xf9 on), prime requisite, hit
     * points and the spell memorized. */
    static const struct { uint8_t class_, level, prime, hp, spell; } kinds[4] = {
        {2, 2, 0, 10, 0}, {0, 0, 2, 7, 3}, {5, 5, 1, 4, 0x0b}, {6, 6, 3, 5, 0},
    };
    const cok_character_layout *L = cok_game_rules(game)->character;
    for (unsigned long i = 0; i < count; ++i) {
        cok_character *c = calloc(1, sizeof *c);
        if (c == NULL) {
            snprintf(game->error, sizeof game->error, "out of memory");
            return false;
        }
        uint8_t *r = c->record;
        cok_field_set8_at(L, r, COK_FIELD_NAME, 0, (uint8_t)strlen(names[i]));
        memcpy(r + cok_field_offset_at(L, COK_FIELD_NAME, 1), names[i], strlen(names[i]));
        const uint8_t *kind = &kinds[i == 7 ? 2 : i % 4].class_; /* HOLT a mage */
        static const uint8_t scores[6] = {15, 13, 13, 14, 15, 12};
        for (int k = 0; k < 6; ++k) {
            uint8_t score = k == kind[2] ? 17 : scores[k];
            cok_field_set8_at(L, r, COK_FIELD_SCORE_BASE, k, score);
            cok_field_set8_at(L, r, COK_FIELD_SCORE, k, score);
        }
        cok_field_set8(L, r, COK_FIELD_RACE, 6); /* human */
        cok_field_set8(L, r, COK_FIELD_CLASS, kind[0]);
        cok_field_set8(L, r, COK_FIELD_DEITY, kind[0] == 0 ? 4 : 0);       /* Mishakal */
        cok_field_set8(L, r, COK_FIELD_MAGIC_ORDER, kind[0] == 5 ? 1 : 0); /* White */
        cok_field_set16(L, r, COK_FIELD_AGE, 20); /* years (its high byte stays 0) */
        cok_field_set8(L, r, COK_FIELD_MAX_HIT_POINTS, kind[3]);
        cok_field_set8(L, r, COK_FIELD_HIT_POINTS, kind[3]);
        cok_field_set8(L, r, COK_FIELD_BASE_HIT_POINTS, kind[3]);
        cok_field_set8(L, r, COK_FIELD_BASE_MOVEMENT, 12);
        cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, kind[1], 1);
        cok_field_set8(L, r, COK_FIELD_GENDER, (uint8_t)(i & 1));
        cok_field_set8(L, r, COK_FIELD_ALIGNMENT, 0);
        cok_field_set8_at(L, r, COK_FIELD_BASE_ATTACKS, 1, 2); /* one a round, doubled */
        cok_field_set8_at(L, r, COK_FIELD_BASE_DICE, 1, 1);
        cok_field_set8_at(L, r, COK_FIELD_BASE_DICE_SIDES, 1, 2);
        cok_field_set8(L, r, COK_FIELD_BASE_AC, 50); /* armour class 10 */
        cok_field_set8(L, r, COK_FIELD_SIZE, 1);     /* one cell in combat */
        /* The icon a new character gets (4def:06dd, 4def:3c61): large for a
         * human, a head by gender, a body by class, the template's colours. */
        cok_field_set8(L, r, COK_FIELD_ICON_SIZE, 2);
        cok_field_set8(L, r, COK_FIELD_ICON_HEAD,
                       cok_field_u8(L, r, COK_FIELD_GENDER) == 1 ? 9 : 5);
        cok_field_set8(L, r, COK_FIELD_ICON_BODY,
                       kind[0] == 0 ? 0x17 : kind[0] == 2 ? 0x18 : kind[0] == 5 ? 0x1d : 5);
        static const uint8_t colours[6] = {0x91, 0xa2, 0xb3, 0xc4, 0xe6, 0xf7};
        memcpy(r + cok_field_offset(L, COK_FIELD_ICON_COLOURS), colours, sizeof colours);
        cok_field_set16_at(L, r, COK_FIELD_COINS, 4, 20); /* steel (the high byte stays 0) */
        cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
        if (kind[4] != 0) {
            cok_field_set8_at(L, r, COK_FIELD_KNOWN_SPELL, kind[4], 1);
            cok_field_set8_at(L, r, COK_FIELD_MEMORIZED, 0, kind[4]);
        }
        if (kind[0] == 2) {
            give_item(c, 0x12, 0x12, 0, 60);     /* Long Sword */
            give_item(c, 0x22, 0x22, 0x2f, 300); /* Chain Mail */
        } else if (kind[0] == 0) {
            give_item(c, 0x08, 0x08, 0, 100);    /* Mace */
            give_item(c, 0x25, 0x25, 0, 50);     /* Shield */
        } else if (kind[0] == 5) {
            give_item(c, 0x0f, 0x0f, 0, 50);     /* Quarter Staff */
        } else {
            give_item(c, 0x13, 0x13, 0, 35);     /* Short Sword */
            give_item(c, 0x1f, 0x1f, 0x30, 150); /* Leather Armor */
        }
        if (i == 6) {
            /* The seventh, a mage, also has Charm Monsters memorized and a
             * Wand of Magic Missiles readied with 5 charges, for casts of
             * weighed picks and from an item. */
            cok_field_set8_at(L, r, COK_FIELD_KNOWN_SPELL, 0x51, 1);
            cok_field_set8_at(L, r, COK_FIELD_MEMORIZED, 1, 0x51);
            give_item(c, 0x34, 0x33, 0, 10);
            c->items[c->item_count - 1][0x3c] = 5;
            c->items[c->item_count - 1][0x3d] = 0x0f;
        }
        if (i == 7) {
            /* The eighth, a mage too, has Stinking Cloud memorized. */
            cok_field_set8_at(L, r, COK_FIELD_KNOWN_SPELL, 0x22, 1);
            cok_field_set8_at(L, r, COK_FIELD_MEMORIZED, 0, 0x22);
        }
        bool ok = cok_character_stats(c, cok_game_rules(game), &game->item_types, game->error,
                                      sizeof game->error) &&
                  cok_character_levels(c, cok_game_rules(game), &game->item_types, game->error,
                                       sizeof game->error);
        if (ok && !cok_party_add(&game->party, cok_game_rules(game), c)) {
            snprintf(game->error, sizeof game->error, "the party is full");
            ok = false;
        }
        if (!ok) {
            cok_character_free(c);
            free(c);
            return false;
        }
        cok_ecl_add_var(&game->vm, COK_VAR_PARTY_SIZE, 1);
    }
    if (!cok_arena_join(game, game->vm.file)) return false;
    game->vm.character = cok_party_record(&game->party, 0);
    return true;
}

static bool number(const char *text, int base, unsigned long max, unsigned long *value)
{
    char *end;
    errno = 0;
    *value = strtoul(text, &end, base);
    return errno == 0 && end != text && *end == '\0' && *value <= max;
}

static player p;

static int run(int argc, char **argv)
{
    p = (player){.keys = cok_key_script_start(NULL), .owed = -1};
    p.port = (cok_game_port){stick_read, stick_wait, &p};
    const char *input = NULL;
    cok_sound_device device = COK_SOUND_TANDY;
    bool sound_given = false;
    cok_display display = COK_DISPLAY_TANDY;
    bool display_given = false;
    const char *screen = NULL;
    unsigned long file = 0, vector = 5, start = 0, fixture = 0, seed = 0;
    bool seeded = false;
    bool still = false, placed = false, play = false, helm = false, front = false, woof = false;
    const char *party = NULL, *load = NULL, *saves = NULL, *config_option = NULL;
    const char *import = NULL;
    const char *characters[COK_PARTY_MAX];
    size_t character_count = 0;
    const cok_rules *rules = &cok_rules_champions;
    /* With no --combat the computer plays its turns, and the player's read
     * the keys (COK_COMBAT_PLAYER) or, with no --keys, pass
     * (COK_COMBAT_UNPORTED); COK_COMBAT_PASS here would have every turn
     * pass by default. */
    cok_combat_stub combat = COK_COMBAT_UNPORTED;
    bool keyed = false, chosen = false;
    long x = 0, y = 0, dir = 0;
    struct { uint16_t address, value; } sets[64];
    size_t set_count = 0;
    struct { unsigned member, id, minutes, value; } effects[64];
    size_t effect_count = 0;
    int i = 1;
    for (; i < argc && strncmp(argv[i], "--", 2) == 0; ++i) {
        const char *option = argv[i];
        bool has_value = i + 1 < argc;
        if (strcmp(option, "--game") == 0 && has_value) {
            rules = cok_rules_find(argv[++i]);
            if (rules == NULL) {
                fprintf(stderr, "--game: unknown game %s (games: %s)\n", argv[i],
                        cok_rules_names());
                return 2;
            }
        } else if (strcmp(option, "--trace") == 0) {
            p.trace = true;
        } else if (strcmp(option, "--mouse") == 0) {
            p.mouse = true;
        } else if (strcmp(option, "--joystick") == 0) {
            p.stick = true;
            p.stick_x = p.stick_y = 129; /* centred */
        } else if (strcmp(option, "--input") == 0 && has_value &&
                   (strcmp(argv[i + 1], "mouse") == 0 || strcmp(argv[i + 1], "joystick") == 0 ||
                    strcmp(argv[i + 1], "none") == 0)) {
            input = argv[++i];
        } else if (strcmp(option, "--still") == 0) {
            still = true;
        } else if (strcmp(option, "--play") == 0) {
            play = true;
        } else if (strcmp(option, "--helm") == 0) {
            helm = true;
        } else if (strcmp(option, "--start-menu") == 0) {
            front = true;
        } else if (strcmp(option, "--woof") == 0) {
            woof = true;
        } else if (strcmp(option, "--keys") == 0 && has_value) {
            p.keys = cok_key_script_start(argv[++i]);
            keyed = true;
        } else if (strcmp(option, "--test-party") == 0 && has_value &&
                   number(argv[i + 1], 10, 8, &fixture) && fixture >= 1) {
            ++i;
        } else if (strcmp(option, "--party") == 0 && has_value) {
            party = argv[++i];
        } else if (strcmp(option, "--character") == 0 && has_value &&
                   character_count < COK_PARTY_MAX) {
            characters[character_count++] = argv[++i];
        } else if (strcmp(option, "--load") == 0 && has_value) {
            load = argv[++i];
        } else if (strcmp(option, "--saves") == 0 && has_value) {
            saves = argv[++i];
        } else if (strcmp(option, "--import") == 0 && has_value) {
            import = argv[++i];
        } else if (strcmp(option, "--config") == 0 && has_value) {
            config_option = argv[++i];
        } else if (strcmp(option, "--combat") == 0 && has_value) {
            static const char *const outcomes[] = {"won",  "fled", "lost", "gods",
                                                   "melee", "auto", "pass"};
            const char *how = argv[++i];
            bool known = false;
            for (size_t k = 0; k < 7; ++k)
                if (strcmp(how, outcomes[k]) == 0) {
                    combat = (cok_combat_stub)(COK_COMBAT_WON + k);
                    known = true;
                }
            if (combat == COK_COMBAT_GODS) helm = true;
            chosen = known;
            if (!known) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(option, "--seed") == 0 && has_value &&
                   number(argv[i + 1], 10, 0xffffffffUL, &seed)) {
            seeded = true;
            ++i;
        } else if (strcmp(option, "--combat-map") == 0 && has_value) {
            p.map = argv[++i];
        } else if (strcmp(option, "--shots") == 0 && has_value) {
            p.shots = argv[++i];
        } else if (strcmp(option, "--sound-calls") == 0) {
            p.calls = true;
        } else if (strcmp(option, "--wav") == 0 && has_value) {
            p.wav_path = argv[++i];
        } else if (strcmp(option, "--sound") == 0 && has_value &&
                   cok_config_sound_option(argv[i + 1], &device)) {
            sound_given = true;
            ++i;
        } else if (strcmp(option, "--display") == 0 && has_value &&
                   cok_config_display_option(argv[i + 1], &display)) {
            display_given = true;
            ++i;
        } else if (strcmp(option, "--screen") == 0 && has_value) {
            screen = argv[++i];
        } else if (strcmp(option, "--file") == 0 && has_value &&
                   number(argv[i + 1], 10, 3, &file) && file >= 1) {
            ++i;
        } else if (strcmp(option, "--vector") == 0 && has_value &&
                   number(argv[i + 1], 10, 4, &vector)) {
            ++i;
        } else if (strcmp(option, "--start") == 0 && has_value &&
                   number(argv[i + 1], 16, 0x9dff, &start) && start >= COK_ECL_BASE) {
            ++i;
        } else if (strcmp(option, "--at") == 0 && has_value &&
                   sscanf(argv[i + 1], "%ld,%ld,%ld", &x, &y, &dir) == 3) {
            placed = true;
            ++i;
        } else if (strcmp(option, "--set") == 0 && has_value && set_count < 64) {
            unsigned address, value;
            if (sscanf(argv[++i], "%x=%x", &address, &value) != 2 || address > 0xffff ||
                value > 0xffff) {
                usage(argv[0]);
                return 2;
            }
            sets[set_count].address = (uint16_t)address;
            sets[set_count++].value = (uint16_t)value;
        } else if (strcmp(option, "--effect") == 0 && has_value && effect_count < 64) {
            unsigned m, id, minutes, value;
            if (sscanf(argv[++i], "%x,%x,%x,%x", &m, &id, &minutes, &value) != 4 || m < 1 ||
                id > 0xff || minutes > 0xffff || value > 0xff) {
                usage(argv[0]);
                return 2;
            }
            effects[effect_count].member = m;
            effects[effect_count].id = id;
            effects[effect_count].minutes = minutes;
            effects[effect_count++].value = value;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (woof && rules->saves.debug == NULL) {
        /* A game with no debug mode reads no Woof (dkk's main reads no
         * argument). */
        fprintf(stderr, "--woof: %s has no Woof (its debug mode)\n", rules->text.name);
        return 2;
    }
    if (keyed && !chosen) combat = COK_COMBAT_PLAYER;
    unsigned long block = 256; /* none given */
    bool has_block = argc - i == 2;
    if (front && (argc - i != 1 || load != NULL || party != NULL || character_count != 0 ||
                  fixture != 0 || play ||
                  vector != 5 || start != 0)) {
        usage(argv[0]);
        return 2;
    }
    if (!front && ((argc - i != 2 && !(load != NULL && argc - i == 1)) ||
        (has_block && !number(argv[i + 1], 0, 255, &block)) ||
        (play && (vector != 5 || start != 0)))) {
        usage(argv[0]);
        return 2;
    }

    if (p.map != NULL) {
        FILE *f = fopen(p.map, "w");
        if (f == NULL) {
            perror(p.map);
            return 1;
        }
        fclose(f);
    }
    if (p.shots != NULL && mkdir(p.shots, 0755) != 0 && errno != EEXIST) {
        perror(p.shots);
        return 1;
    }
    if (sound_given && p.wav_path == NULL) {
        fprintf(stderr, "--sound needs --wav: eclplay renders sound only to a file\n");
        return 2;
    }
    /* KRYNN.CFG, as startup reads it (11c7:031a): from the title, the
     * setup when there is none, and the note to Tandy users. */
    cok_config config = {.display = COK_DISPLAY_TANDY, .sound = COK_SOUND_TANDY};
    char config_path[1024], config_write[1024];
    bool found = cok_config_find(rules, config_option, saves, argv[i], config_path, config_write,
                                 sizeof config_path);
    bool config_read;
    if (front) {
        static cok_crt crt;
        crt.log = crt_log;
        crt.context = &p;
        cok_crt_init(&crt);
        p.crt = &crt;
        cok_keyboard crt_keys = {next_key, &p, NULL, NULL};
        cok_setup_hooks crt_hooks = {crt_delay, &p, &p.port, &p.ended,
                                     input != NULL && strcmp(input, "joystick") == 0};
        char error[1200];
        cok_setup_status status = cok_setup_startup(&crt, &crt_keys, &crt_hooks, rules,
                                                    config_path, found, &config, error,
                                                    sizeof error);
        p.crt = NULL;
        if (status == COK_SETUP_ENDED) {
            printf("(out of keys in the setup)\n");
            return 0;
        }
        if (status != COK_SETUP_OK) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        if (!found && config_path[0] == '\0')
            fprintf(stderr, "setup: %s written nowhere (no --saves or --config)\n",
                    rules->files.config);
        config_read = true;
    } else {
        config_read = found && cok_config_read_file_game(rules, config_path, &config, p.stick);
        if (input != NULL && strcmp(input, "joystick") == 0 && !config.joystick) {
            /* --input joystick without the file's thresholds: calibrated
             * here, as at startup (cok_setup_calibrate). */
            static cok_crt crt;
            crt.log = crt_log;
            crt.context = &p;
            cok_crt_init(&crt);
            cok_setup_hooks crt_hooks = {crt_delay, &p, &p.port, &p.ended, true};
            if (!cok_setup_calibrate(&crt, &crt_hooks, rules, &config)) {
                printf("(out of keys in the calibration)\n");
                return 0;
            }
            cok_crt_log_rows(&crt);
        }
    }
    if (!display_given) display = config.display;
    if (!display_given && !config_read)
        cok_config_missing(rules, "display", config_option, saves, argv[i]);
    if (p.wav_path != NULL) {
        p.wav = fopen(p.wav_path, "wb");
        p.sound = malloc(sizeof *p.sound);
        if (p.wav == NULL || p.sound == NULL) {
            perror(p.wav_path);
            return 1;
        }
        uint8_t header[44];
        wav_header(header, 0);
        fwrite(header, 1, sizeof header, p.wav);
        if (!sound_given && !config_read)
            cok_config_missing(rules, "sound", config_option, saves, argv[i]);
        if (!sound_given && config_read) device = config.sound;
        cok_sound_init(p.sound, device);
        if (!cok_sound_load(p.sound, rules, argv[i]))
            fprintf(stderr, "sound: no music: %s\n", p.sound->music_error);
    }
    static cok_adventure game;
    p.game = &game;
    cok_keyboard keys = {next_key, &p, NULL, NULL};
    cok_adventure_hooks hooks = {.unported = unported, .log = log_line, .trace = trace,
                                 .key_pending = key_pending, .battlefield = battlefield,
                                 .delay = delay, .flush = flush, .device_wait = device_wait,
                                 .sound = p.wav_path != NULL || p.calls ? sound : NULL,
                                 .clock_speed = clock_speed, .context = &p};
    if (!cok_adventure_open_display(&game, rules, argv[i], display, &keys, &hooks)) {
        fprintf(stderr, "%s\n", game.error);
        cok_adventure_close(&game);
        return 1;
    }
    if (import == NULL) import = saves;
    if ((saves != NULL && strlen(saves) >= sizeof game.save_dir) ||
        (import != NULL && strlen(import) >= sizeof game.import_dir)) {
        fprintf(stderr, "%s: directory name too long\n",
                saves != NULL && strlen(saves) >= sizeof game.save_dir ? saves : import);
        cok_adventure_close(&game);
        return 1;
    }
    if (saves != NULL) snprintf(game.save_dir, sizeof game.save_dir, "%s", saves);
    if (import != NULL) snprintf(game.import_dir, sizeof game.import_dir, "%s", import);
    if (found || (front && config_write[0] != '\0'))
        snprintf(game.config_path, sizeof game.config_path, "%s", config_path);
    snprintf(game.config_write, sizeof game.config_write, "%s", config_write);
    /* The mouse and joystick: KRYNN.CFG's third line, or --input, on the
     * machine --mouse and --joystick give (none by default); found at
     * startup, or here past it. */
    if (input != NULL) {
        config.mouse = strcmp(input, "mouse") == 0;
        config.joystick = strcmp(input, "joystick") == 0;
    }
    game.mouse.driver.installed = p.mouse;
    game.port = p.port;
    cok_adventure_devices(&game, config.mouse, config.joystick, config.joystick_x,
                          config.joystick_y);
    if (!front) cok_adventure_mouse_startup(&game);
    /* Past main's install of the effects driver (1000:0160), unless the
     * run starts from the title. */
    if (!front) cok_adventure_sound(&game, COK_SOUND_INSTALL, 0);
    bool loaded = true;
    if (load != NULL) loaded = cok_adventure_restore(&game, load);
    if (loaded && party != NULL) loaded = cok_adventure_load_party(&game, party);
    if (loaded && character_count > 0)
        loaded = cok_adventure_add_characters(&game, characters, character_count);
    if (loaded && fixture > 0) loaded = test_party(&game, fixture);
    for (size_t k = 0; loaded && k < effect_count; ++k) {
        if (effects[k].member > game.party.count) {
            snprintf(game.error, sizeof game.error, "--effect: no party member %u",
                     effects[k].member);
            loaded = false;
        } else if (cok_character_add_effect(game.party.members[effects[k].member - 1],
                                            (uint8_t)effects[k].id, (uint16_t)effects[k].minutes,
                                            (uint8_t)effects[k].value, true) == NULL) {
            snprintf(game.error, sizeof game.error, "out of memory");
            loaded = false;
        }
    }
    if (!loaded) {
        fprintf(stderr, "%s\n", game.error);
        cok_adventure_close(&game);
        return 1;
    }
    if (still) game.animate = false;
    if (seeded) game.vm.seed = (uint32_t)seed;
    game.combat_stub = combat;
    game.helm = helm;
    game.woof = woof;
    if (front) {
        game.combat_stub = combat;
        game.helm = helm;
        if (seeded) game.vm.seed = (uint32_t)seed;
        cok_ecl_status status = cok_start_game(&game, woof);
        if (screen != NULL) save(&p, screen);
        int result = 0;
        if (status != COK_ECL_OK) {
            fprintf(stderr, "%s\n", game.error[0] != '\0' ? game.error
                                                           : cok_ecl_status_string(status));
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
        cok_adventure_close(&game);
        return result;
    }
    if (!has_block) {
        /* As 2fd3:3c28 resumes: the block saved in 0x4bf2, or a new game's
         * (0x24; dkk 0x10). */
        block = cok_ecl_var(&game.vm, COK_VAR_BLOCK) != 0 ? cok_ecl_var(&game.vm, COK_VAR_BLOCK)
                                                          : rules->start.first_block;
        if (file == 0) file = game.vm.file;
    }
    if (file == 0) {
        for (unsigned n = 1; n <= 3 && file == 0; ++n) {
            size_t size;
            uint8_t *data = cok_adventure_record(&game, rules->archives.ecl, n, (uint8_t)block,
                                                 &size);
            if (data != NULL) file = n;
            free(data);
        }
        if (file == 0) {
            fprintf(stderr, "no ECL file holds block %lu\n", block);
            cok_adventure_close(&game);
            return 1;
        }
    }
    game.vm.file = (uint8_t)file;
    if (placed) {
        cok_ecl_store(&game.vm, 0xc04b, (uint16_t)x);
        cok_ecl_store(&game.vm, 0xc04c, (uint16_t)y);
        game.vm.direction = (uint8_t)(dir & 6);
        /* The square's byte (DS:6d89) for a load vector run before any
         * step; a block entered recomputes it anyway. */
        game.vm.square = cok_view_square(&game.view, game.vm.map_x, game.vm.map_y);
    }
    cok_adventure_frame(&game);

    cok_ecl_status status;
    if (vector == 5 && start == 0) {
        for (size_t s = 0; s < set_count; ++s) cok_ecl_store(&game.vm, sets[s].address, sets[s].value);
        /* A loaded game resumes where it was saved as 2fd3:3c28 starts
         * (its load vector alone); a block named is entered as 3b47 enters
         * one. */
        status = has_block ? cok_adventure_enter_block(&game, (uint8_t)block)
                           : cok_adventure_enter(&game, (uint8_t)block);
        if (play && status == COK_ECL_OK) status = cok_adventure_play(&game);
    } else {
        status = cok_adventure_load(&game, (uint8_t)block);
        for (size_t s = 0; s < set_count; ++s) cok_ecl_store(&game.vm, sets[s].address, sets[s].value);
        uint16_t address = start != 0 ? (uint16_t)start : game.vm.vectors[vector];
        if (status == COK_ECL_OK) status = cok_ecl_run(&game.vm, address);
    }
    if (screen != NULL) save(&p, screen);
    int result = 0;
    if (status == COK_ECL_LOAD_FAILED || status == COK_ECL_EFFECT_FAILED ||
        status == COK_ECL_UNDEFINED || status == COK_ECL_UNPORTED) {
        fprintf(stderr, "%s\n", game.error);
        result = 1;
    } else if (status != COK_ECL_OK) {
        fprintf(stderr, "block %u at %04x: %s\n", game.vm.block, game.vm.ip,
                cok_ecl_status_string(status));
        result = 1;
    } else if (game.quit) {
        printf("(quit to DOS in block %u)\n", game.vm.block);
    } else if (game.party_killed) {
        printf("(the party was killed in block %u)\n", game.vm.block);
    } else if (game.input_ended) {
        printf("(out of keys in block %u at %04x)\n", game.vm.block, game.vm.ip);
    } else {
        printf("(done in block %u)\n", game.vm.block);
    }
    cok_adventure_close(&game);
    return result;
}

int main(int argc, char **argv)
{
    int result = run(argc, argv);
    int written = finish_wav(&p);
    return result != 0 ? result : written;
}
