/* Run an ECL block on the adventure screen without a display: keys come
 * from the command line, text and menus go to standard output, and the
 * screen can be saved as a BMP whenever the game waits for a key. */
#include "adventure.h"
#include "arena.h"

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
    const char *map;      /* File for each battle's map, or NULL. */
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
            "  --test-party N  add N (1-8) characters made up for testing, without saved\n"
            "                  games: fighters, clerics, mages and thieves of level 1\n"
            "  --party SAVE    add the characters of saved game SAVE (SAVGAMA.DAT) to the\n"
            "                  party, from the files beside it\n"
            "  --load SAVE     load saved game SAVE and its party first; BLOCK defaults to\n"
            "                  the block it was saved in\n"
            "  --saves DIR     write saved games (camp's Save) to DIR\n"
            "  --combat HOW    decide COMBAT's battle in place of its rounds, whose turns\n"
            "                  pass: won (every monster against the party drops), fled\n"
            "                  (the party flees) or lost (the party dies); or gods, the\n"
            "                  original's Helm cheat at each player's turn\n"
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
    fprintf(f, "battle in block %u: view %d,%d, %u combatants\n", game->vm.block, c->view_x,
            c->view_y, c->count - 1u);
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y) {
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) {
            uint8_t v = c->cells[y][x], who = c->occupant[y][x];
            const cok_terrain *t = &cok_combat_terrain[v < COK_COMBAT_TERRAINS ? v : 0];
            char ch = who != 0 ? mark(who) : v == COK_COMBAT_FLOOR || v == 0x36 ? '.'
                    : t->cost == 0xff ? '#' : v == 0x1a ? 'T' : v == 0x1b ? 'h'
                    : v == COK_COMBAT_BODY ? '_' : t->cost == 2 ? ':' : t->cost == 4 ? '~' : ',';
            fputc(ch, f);
        }
        fputc('\n', f);
    }
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y) {
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) fprintf(f, "%02x", c->cells[y][x]);
        fputc('\n', f);
    }
    for (unsigned n = 1; n < c->count; ++n) {
        const cok_combatant *e = &c->combatant[n];
        const uint8_t *r = e->character->record;
        fprintf(f, "%c %u %.*s: at %d,%d size %u side %u facing %u\n", mark(n), n,
                r[0] > 15 ? 15 : r[0], (const char *)r + 1, e->x, e->y, e->size, r[0x18a],
                e->character->combat->facing);
    }
    for (unsigned k = 0; k < c->bodies; ++k)
        fprintf(f, "body at %d,%d on %02x\n", c->body[k].x, c->body[k].y, c->body[k].cell);
    fputc('\n', f);
    fclose(f);
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
 * in turn, human, of level 1, with their weapons and armour readied, a
 * little steel, the cleric's Cure Light Wounds and the mage's Detect Magic
 * memorized; their stats computed as a character's are when it loads
 * (6346:0d20, 66c2:0433), each counted in 0x7f3e and the first selected. */
static bool test_party(cok_adventure *game, unsigned long count)
{
    static const char *const names[8] = {"ALDA", "BRAM", "CERA", "DUNN",
                                         "ELIN", "FARO", "GWEN", "HOLT"};
    /* Class (+0x5b), the level's byte (+0xf9 on), prime requisite, hit
     * points and the spell memorized. */
    static const struct { uint8_t class_, level, prime, hp, spell; } kinds[4] = {
        {2, 2, 0, 10, 0}, {0, 0, 2, 7, 3}, {5, 5, 1, 4, 0x0b}, {6, 6, 3, 5, 0},
    };
    for (unsigned long i = 0; i < count; ++i) {
        cok_character *c = calloc(1, sizeof *c);
        if (c == NULL) {
            snprintf(game->error, sizeof game->error, "out of memory");
            return false;
        }
        uint8_t *r = c->record;
        r[0] = (uint8_t)strlen(names[i]);
        memcpy(r + 1, names[i], strlen(names[i]));
        const uint8_t *kind = &kinds[i % 4].class_;
        static const uint8_t scores[6] = {15, 13, 13, 14, 15, 12};
        for (int k = 0; k < 6; ++k)
            r[0x10 + 2 * k] = r[0x11 + 2 * k] = k == kind[2] ? 17 : scores[k];
        r[0x5a] = 6;               /* human */
        r[0x5b] = kind[0];
        r[0x5d] = kind[0] == 0 ? 4 : 0; /* Mishakal */
        r[0x5e] = kind[0] == 5 ? 1 : 0;  /* White */
        r[0x60] = 20;              /* years */
        r[0x62] = r[0x197] = r[0x11b] = kind[3];
        r[0xd5] = 12;              /* movement */
        r[0xf9 + kind[1]] = 1;
        r[0x109] = (uint8_t)(i & 1);
        r[0x10a] = 0;
        r[0x10b] = 2;              /* one attack a round, doubled */
        r[0x10d] = 1;
        r[0x10f] = 2;
        r[0x113] = 50;             /* armour class 10 */
        r[0xcf] = 1;               /* one cell in combat */
        /* The icon a new character gets (4def:06dd, 4def:3c61): large for a
         * human, a head by gender, a body by class, the template's colours. */
        r[0x138] = 2;
        r[0x135] = r[0x109] == 1 ? 9 : 5;
        r[0x136] = kind[0] == 0 ? 0x17 : kind[0] == 2 ? 0x18 : kind[0] == 5 ? 0x1d : 5;
        static const uint8_t colours[6] = {0x91, 0xa2, 0xb3, 0xc4, 0xe6, 0xf7};
        memcpy(r + 0x139, colours, sizeof colours);
        r[0xeb + 8] = 20;          /* steel */
        r[0x189] = 1;
        if (kind[4] != 0) {
            r[0x62 + kind[4]] = 1; /* known */
            r[0x1e] = kind[4];     /* memorized */
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
        bool ok = cok_character_stats(c, &game->item_types, game->error, sizeof game->error) &&
                  cok_character_levels(c, &game->item_types, game->error, sizeof game->error);
        if (ok && !cok_party_add(&game->party, c)) {
            snprintf(game->error, sizeof game->error, "the party is full");
            ok = false;
        }
        if (!ok) {
            cok_character_free(c);
            free(c);
            return false;
        }
        ++game->vm.mem7c00[0x33e];
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

int main(int argc, char **argv)
{
    player p = {.keys = "", .pending = -1};
    const char *screen = NULL;
    unsigned long file = 0, vector = 5, start = 0, fixture = 0, seed = 0;
    bool seeded = false;
    bool still = false, placed = false, play = false, helm = false;
    const char *party = NULL, *load = NULL, *saves = NULL;
    cok_combat_stub combat = COK_COMBAT_UNPORTED;
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
        } else if (strcmp(option, "--helm") == 0) {
            helm = true;
        } else if (strcmp(option, "--keys") == 0 && has_value) {
            p.keys = argv[++i];
        } else if (strcmp(option, "--test-party") == 0 && has_value &&
                   number(argv[i + 1], 10, 8, &fixture) && fixture >= 1) {
            ++i;
        } else if (strcmp(option, "--party") == 0 && has_value) {
            party = argv[++i];
        } else if (strcmp(option, "--load") == 0 && has_value) {
            load = argv[++i];
        } else if (strcmp(option, "--saves") == 0 && has_value) {
            saves = argv[++i];
        } else if (strcmp(option, "--combat") == 0 && has_value) {
            static const char *const outcomes[] = {"won", "fled", "lost", "gods"};
            const char *how = argv[++i];
            for (size_t k = 0; k < 4; ++k)
                if (strcmp(how, outcomes[k]) == 0) combat = (cok_combat_stub)(COK_COMBAT_WON + k);
            if (combat == COK_COMBAT_GODS) helm = true;
            if (combat == COK_COMBAT_UNPORTED) {
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
    static cok_adventure game;
    p.game = &game;
    cok_keyboard keys = {next_key, &p};
    cok_adventure_hooks hooks = {.unported = unported, .log = log_line, .trace = trace,
                                 .key_pending = key_pending, .battlefield = battlefield,
                                 .context = &p};
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
    if (loaded && fixture > 0) loaded = test_party(&game, fixture);
    if (!loaded) {
        fprintf(stderr, "%s\n", game.error);
        cok_adventure_close(&game);
        return 1;
    }
    if (still) game.animate = false;
    if (seeded) game.vm.seed = (uint32_t)seed;
    game.combat_stub = combat;
    game.helm = helm;
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
