/* Run an ECL block on the adventure screen without a display: keys come
 * from the command line, text and menus go to standard output, and the
 * screen can be saved as a BMP whenever the game waits for a key. */
#include "adventure.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    const char *keys;     /* Remaining key script. */
    int pending;          /* Scan code after a 0, or -1. */
    const char *shots;    /* Directory for BMPs, or NULL. */
    unsigned shot;
    bool trace;
    cok_adventure *game;
} player;

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [options] ASSETS BLOCK\n"
            "       %s [options] --load SAVE ASSETS [BLOCK]\n"
            "Run ECL block BLOCK (as the game does on entering it) from the DAX files in\n"
            "ASSETS, printing its text, menus and choices.\n"
            "  --keys KEYS     keys to type; \\r Enter, \\e Escape, \\b Backspace,\n"
            "                  \\< \\> \\^ \\v the arrows, \\\\ a backslash; \\k presses the\n"
            "                  next key while the party rests\n"
            "  --play          then take adventure commands until the keys run out\n"
            "  --party SAVE    add the characters of saved game SAVE (SAVGAMA.DAT) to the\n"
            "                  party, from the files beside it\n"
            "  --load SAVE     load saved game SAVE and its party first; BLOCK defaults to\n"
            "                  the block it was saved in\n"
            "  --saves DIR     write saved games (camp's Save) to DIR\n"
            "  --shots DIR     save DIR/NNN.bmp each time the game waits for a key\n"
            "  --screen FILE   save the final screen as FILE (BMP)\n"
            "  --file N        ECL file 1-3 (default: the first holding BLOCK)\n"
            "  --vector N      run only vector N (0-4) instead of entering the block\n"
            "  --start ADDR    run only from code address ADDR (hex)\n"
            "  --at X,Y,DIR    party position and facing (0, 2, 4 or 6)\n"
            "  --set ADDR=VAL  set a variable before running, in hex (repeatable)\n"
            "  --still         load only the first frame of each picture\n"
            "  --trace         print each instruction's address and name\n",
            program, program);
}

static void save(player *p, const char *path)
{
    cok_image image = cok_picture_frame(&p->game->screen, 0);
    cok_image_status status = cok_image_bmp(&image, path);
    if (status != COK_IMAGE_OK)
        fprintf(stderr, "%s: %s\n", path, cok_image_status_string(status));
}

static int next_key(void *context)
{
    player *p = context;
    if (p->pending >= 0) {
        int key = p->pending;
        p->pending = -1;
        return key;
    }
    if (p->shots != NULL) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%03u.bmp", p->shots, p->shot++);
        save(p, path);
    }
    /* A key pressed while resting is read as any other. */
    if (p->keys[0] == '\\' && p->keys[1] == 'k') p->keys += 2;
    if (*p->keys == '\0') return -1;
    int key = (unsigned char)*p->keys++;
    if (key != '\\' || *p->keys == '\0') return key;
    switch (*p->keys++) {
    case 'r': return 13;
    case 'e': return 27;
    case 'b': return 8;
    case '<': p->pending = 0x4b; return 0;
    case '>': p->pending = 0x4d; return 0;
    case '^': p->pending = 0x48; return 0;
    case 'v': p->pending = 0x50; return 0;
    default: return (unsigned char)p->keys[-1];
    }
}

/* \k in the keys: a key waits to be read, which the rest loop polls. */
static bool key_pending(cok_adventure *game, void *context)
{
    (void)game;
    player *p = context;
    if (p->keys[0] != '\\' || p->keys[1] != 'k') return false;
    p->keys += 2;
    return true;
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    (void)context;
    printf("%s: %s\n", kind, text);
}

static void unported(cok_adventure *game, void *context)
{
    (void)context;
    const char *name = cok_ecl_opcode_name(game->vm.opcode);
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
    const char *name = cok_ecl_opcode_name(opcode);
    printf("  %04x %s\n", vm->ip, name != NULL ? name : "?");
}

static bool number(const char *text, int base, unsigned long max, unsigned long *value)
{
    char *end;
    errno = 0;
    *value = strtoul(text, &end, base);
    return errno == 0 && end != text && *end == '\0' && *value <= max;
}

int main(int argc, char **argv)
{
    player p = {.keys = "", .pending = -1};
    const char *screen = NULL;
    unsigned long file = 0, vector = 5, start = 0;
    bool still = false, placed = false, play = false;
    const char *party = NULL, *load = NULL, *saves = NULL;
    long x = 0, y = 0, dir = 0;
    struct { uint16_t address, value; } sets[64];
    size_t set_count = 0;
    int i = 1;
    for (; i < argc && strncmp(argv[i], "--", 2) == 0; ++i) {
        const char *option = argv[i];
        bool has_value = i + 1 < argc;
        if (strcmp(option, "--trace") == 0) {
            p.trace = true;
        } else if (strcmp(option, "--still") == 0) {
            still = true;
        } else if (strcmp(option, "--play") == 0) {
            play = true;
        } else if (strcmp(option, "--keys") == 0 && has_value) {
            p.keys = argv[++i];
        } else if (strcmp(option, "--party") == 0 && has_value) {
            party = argv[++i];
        } else if (strcmp(option, "--load") == 0 && has_value) {
            load = argv[++i];
        } else if (strcmp(option, "--saves") == 0 && has_value) {
            saves = argv[++i];
        } else if (strcmp(option, "--shots") == 0 && has_value) {
            p.shots = argv[++i];
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
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    unsigned long block = 256; /* none given */
    bool has_block = argc - i == 2;
    if ((argc - i != 2 && !(load != NULL && argc - i == 1)) ||
        (has_block && !number(argv[i + 1], 0, 255, &block)) ||
        (play && (vector != 5 || start != 0))) {
        usage(argv[0]);
        return 2;
    }

    if (p.shots != NULL && mkdir(p.shots, 0755) != 0 && errno != EEXIST) {
        perror(p.shots);
        return 1;
    }
    static cok_adventure game;
    p.game = &game;
    cok_keyboard keys = {next_key, &p};
    cok_adventure_hooks hooks = {.unported = unported, .log = log_line, .trace = trace,
                                 .key_pending = key_pending, .context = &p};
    if (!cok_adventure_open(&game, argv[i], &keys, &hooks)) {
        fprintf(stderr, "%s\n", game.error);
        cok_adventure_close(&game);
        return 1;
    }
    if (saves != NULL && strlen(saves) >= sizeof game.save_dir) {
        fprintf(stderr, "%s: directory name too long\n", saves);
        cok_adventure_close(&game);
        return 1;
    }
    if (saves != NULL) snprintf(game.save_dir, sizeof game.save_dir, "%s", saves);
    bool loaded = true;
    if (load != NULL) loaded = cok_adventure_restore(&game, load);
    if (loaded && party != NULL) loaded = cok_adventure_load_party(&game, party);
    if (!loaded) {
        fprintf(stderr, "%s\n", game.error);
        cok_adventure_close(&game);
        return 1;
    }
    if (still) game.animate = false;
    if (!has_block) {
        /* As 2fd3:3c28 resumes: the block saved in 0x4bf2, or 0x24. */
        block = game.vm.mem4b00[0xf2] != 0 ? game.vm.mem4b00[0xf2] : 0x24;
        if (file == 0) file = game.vm.file;
    }
    if (file == 0) {
        for (unsigned n = 1; n <= 3 && file == 0; ++n) {
            size_t size;
            uint8_t *data = cok_adventure_record(&game, "ECL", n, (uint8_t)block, &size);
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
    }
    cok_adventure_frame(&game);

    cok_ecl_status status;
    if (vector == 5 && start == 0) {
        for (size_t s = 0; s < set_count; ++s) cok_ecl_store(&game.vm, sets[s].address, sets[s].value);
        status = cok_adventure_enter(&game, (uint8_t)block);
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
        status == COK_ECL_UNDEFINED) {
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
