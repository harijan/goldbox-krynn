#define _POSIX_C_SOURCE 200809L /* mkdtemp */

#include "arena.h"
#include "camp.h"
#include "create.h"
#include "icon.h"
#include "modify.h"
#include "roster.h"
#include "start.h"
#include "train.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Keys from a string: \x01 stands for the 0 that starts an extended key,
 * \x02 makes the next key wait (KeyPressed) and \x03 is a menu's time
 * running out. */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[16384];
    unsigned waits;
} script;

static int scripted(void *context)
{
    script *s = context;
    while (s->at < s->length && s->keys[s->at] == 2) ++s->at;
    if (s->at == s->length) return -1;
    char c = s->keys[s->at++];
    return c == 1 ? 0 : c == 3 ? COK_KEY_TIMEOUT : (unsigned char)c;
}

static bool pending(cok_adventure *game, void *context)
{
    (void)game;
    script *s = context;
    if (s->at == s->length || s->keys[s->at] != 2) return false;
    ++s->at;
    return true;
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

static void delay(cok_adventure *game, unsigned ms, void *context)
{
    (void)game;
    (void)ms;
    script *s = context;
    ++s->waits;
}

static script s;
static cok_adventure game;
static char dir[64];

static void keys(const char *k)
{
    s.keys = k;
    s.at = 0;
    s.length = strlen(k);
    s.log[0] = '\0';
    s.waits = 0;
    game.input_ended = false;
    game.quit = false;
    game.vm.abort = false;
    game.vm.status = COK_ECL_OK;
    game.error[0] = '\0';
}

static bool logged(const char *what)
{
    return strstr(s.log, what) != NULL;
}

static void empty_party(void)
{
    cok_party_free(&game.party);
    game.vm.character = NULL;
    game.vm.mem7c00[0x33e] = 0;
}

/* A character of class (a level index +0xf9 on) at level, with name. */
static cok_character *member(const char *name, size_t class_, uint8_t level)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    c->record[0] = (uint8_t)strlen(name);
    memcpy(c->record + 1, name, strlen(name));
    c->record[0xf9 + class_] = level;
    c->record[0xd6] = level;
    for (size_t i = 0; i < 6; ++i) c->record[0x10 + 2 * i] = c->record[0x11 + 2 * i] = 12;
    c->record[0x62] = c->record[0x197] = 10;
    c->record[0x189] = 1;
    c->record[0x5a] = 6;
    return c;
}

static cok_character *add(cok_character *c)
{
    CHECK(cok_party_add(&game.party, c));
    ++game.vm.mem7c00[0x33e];
    if (game.vm.character == NULL) game.vm.character = c->record;
    return c;
}

static bool cell_black(int x, int y)
{
    for (int row = 0; row < 8; ++row)
        for (size_t b = 0; b < 4; ++b)
            if (game.screen.pixels[(size_t)(y * 8 + row) * 160 + (size_t)x * 4 + b] != 0) return false;
    return true;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (size_t i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}


/* A character for the cases below: name "T", scores 12 but strength and
 * constitution, able to act, its highest level its highest. */
typedef struct {
    struct {
        uint8_t race, class_, knight, deity, magic, strength, constitution;
        uint32_t experience;
        uint8_t max, hp, full, status, gender, exceptional;
    } f;
    uint8_t levels[8];
} trainee;

/* Generated from the original in an 8086 emulator (genvectors.py). */
/* Creation (4def:06dd) as the original made it: seed, list picks (race,
 * gender, class, deity or 0, alignment), rerolls, and the hash of the
 * record, items and effects saved, then the seed after. */
static const struct { uint32_t seed; uint8_t race, gender, class_, deity, align, rerolls;
                      uint32_t hash, after; } created[] = {
    {0xf2a74de4, 1, 2, 1, 3, 3, 0, 0x7cbd7fa0, 0x7bd1200e},
    {0x892f902b, 1, 2, 2, 0, 5, 0, 0x68e07393, 0xbac5d3e6},
    {0x36f675cc, 1, 1, 3, 0, 4, 2, 0xb19268fe, 0x6be5a01d},
    {0x11e20b8f, 1, 1, 4, 0, 3, 2, 0xf8cf8e56, 0x394cfbca},
    {0xf29d0da9, 1, 2, 5, 1, 1, 0, 0x69856c5d, 0xf2a90b52},
    {0x2217bead, 1, 2, 6, 1, 1, 0, 0x60f24a58, 0x09481534},
    {0x2e44158b, 1, 1, 7, 5, 1, 0, 0xa8dcb3f6, 0xfdf9de5c},
    {0xb64ce422, 1, 1, 8, 5, 2, 2, 0xd1b7833c, 0x1b7c46f6},
    {0x506bf2ef, 1, 2, 9, 0, 3, 1, 0x2fdd8d5c, 0x07737f9a},
    {0xcb5c7427, 2, 1, 1, 2, 1, 1, 0x8c4ed38f, 0x6599e8d4},
    {0x86734721, 2, 2, 2, 0, 6, 2, 0x50911c2f, 0x0b07cc82},
    {0xfaecbd38, 2, 1, 3, 0, 5, 2, 0x75800658, 0x40a85b39},
    {0x5790f82e, 2, 2, 4, 0, 2, 0, 0xaa3e3f53, 0x9d23f8f6},
    {0xd17f9aca, 2, 2, 5, 0, 3, 1, 0xa4a77ee8, 0x15e66056},
    {0xcc011cdd, 2, 1, 6, 5, 1, 1, 0xef4ad0ee, 0x559f3cd7},
    {0x10a3d6b2, 2, 2, 7, 4, 3, 2, 0x3445844f, 0x43a1f431},
    {0xb774eb52, 2, 2, 8, 3, 1, 2, 0x9a8a548c, 0x85d62559},
    {0x1df9fd78, 2, 1, 9, 3, 1, 1, 0x5697e0bd, 0x88dffa33},
    {0x3f63af83, 2, 2, 10, 0, 4, 0, 0xe80cfdee, 0xa3076085},
    {0x2a96fb1a, 2, 2, 11, 0, 3, 1, 0x02ddcf9a, 0x3d1fc02d},
    {0xe2257159, 2, 2, 12, 0, 3, 1, 0x8229978c, 0xbff9528c},
    {0xb4d66a3a, 2, 2, 13, 0, 3, 2, 0x4b216de0, 0xd6d9b975},
    {0x26a2c0bd, 3, 1, 1, 2, 1, 0, 0x62ad747b, 0xee8a2b14},
    {0xa8948c89, 3, 1, 2, 0, 4, 0, 0xd77ae030, 0xd9b3b376},
    {0x43435cc5, 3, 1, 3, 0, 2, 2, 0x145a2d9d, 0xf495e74b},
    {0x88daf401, 3, 2, 4, 0, 1, 0, 0xc9227df4, 0xfaa2e89b},
    {0x8f2c6ec8, 3, 2, 5, 0, 2, 2, 0x22f8ab8f, 0x130d6d61},
    {0x1a81682c, 3, 2, 6, 0, 1, 0, 0xe4790088, 0x41bfcbf9},
    {0xfc132d0d, 3, 2, 7, 1, 1, 0, 0xc0c61ff1, 0xb4d6f186},
    {0x99c94309, 3, 1, 8, 3, 1, 0, 0x2f4c8c2c, 0x05d36ce8},
    {0x068739fa, 3, 1, 9, 5, 4, 0, 0x006febb2, 0x10a25d4f},
    {0x5d39d0a8, 3, 1, 10, 6, 1, 2, 0x42f517fc, 0x841aa85c},
    {0x774b15d7, 3, 2, 11, 0, 3, 0, 0x26bd0b87, 0x0987ebf9},
    {0x24e4e25a, 3, 2, 12, 0, 3, 1, 0xe3a10d8c, 0xf7f0366d},
    {0xb12aa1f6, 3, 1, 13, 0, 1, 1, 0x45cb4cf5, 0xad7b0c6a},
    {0x87322e25, 3, 1, 14, 0, 3, 1, 0x089c64cd, 0x280e1546},
    {0x84b5a818, 4, 1, 1, 1, 3, 0, 0xc47b90c7, 0x172c46d5},
    {0xda45e18a, 4, 1, 2, 0, 4, 0, 0x92eeadf7, 0xf2949d1b},
    {0x7e26f36a, 4, 1, 3, 0, 1, 1, 0x0cdf60bf, 0x26742bc2},
    {0x78e4b98d, 4, 1, 4, 1, 3, 2, 0xa5b9dc36, 0xd1a5055d},
    {0xf979d04a, 4, 1, 5, 1, 1, 0, 0xaf03f4e8, 0xd10ba492},
    {0x3a12917c, 4, 1, 6, 0, 2, 0, 0x416e7130, 0x17050660},
    {0x007d1034, 5, 2, 1, 1, 1, 0, 0xf1d422d6, 0xb3d0afa1},
    {0xe8e72789, 5, 1, 2, 0, 4, 0, 0x1b4dbd48, 0xf9519a76},
    {0xa2c68e45, 5, 1, 3, 0, 3, 2, 0x4cde8ff8, 0xf5932ccb},
    {0x7691b06f, 5, 1, 4, 0, 3, 0, 0x62a4f2dd, 0x732d5b9a},
    {0x20859634, 5, 1, 5, 1, 4, 0, 0xaf6682b3, 0x298f3aeb},
    {0xeffddeea, 5, 1, 6, 1, 1, 0, 0x5513d760, 0xafdc2604},
    {0xa6511445, 5, 1, 7, 1, 2, 0, 0x88f03b08, 0x8c4f4bed},
    {0x3606defc, 5, 2, 8, 0, 1, 1, 0x23bb34ef, 0x362c8f67},
    {0x9620bf0d, 6, 2, 1, 5, 4, 0, 0x9a02c354, 0x74e5c3ce},
    {0xbd6b881a, 6, 2, 2, 0, 6, 2, 0x3d110f84, 0x340eedf1},
    {0x82b33599, 6, 2, 3, 0, 1, 0, 0x22d9bc84, 0x916915ca},
    {0x265974a7, 6, 1, 4, 0, 2, 0, 0x7b3a50b2, 0x3a54d5f2},
    {0x0fcf31ca, 6, 2, 5, 5, 1, 0, 0x85813458, 0xc78b2ae9},
    {0x30f97058, 6, 1, 6, 2, 1, 2, 0xf081d4c9, 0x0d1cf3ff},
    {0x1038f0b5, 6, 2, 7, 5, 3, 0, 0xacfec99e, 0xe311f65d},
    {0xb156d1ad, 6, 2, 8, 0, 3, 2, 0xf285d109, 0xfb1ef53b},
    {0xf10637ce, 7, 2, 1, 5, 2, 2, 0xebcb9572, 0x73966fa3},
    {0x231b3e14, 7, 1, 2, 0, 4, 2, 0x3f5c4cc4, 0xe2f16d85},
    {0x50e40d54, 7, 1, 3, 0, 4, 0, 0xa7eea4c2, 0x684dbf41},
    {0xc6e50df2, 7, 2, 4, 0, 1, 1, 0xe448ba4c, 0x9ee5e7e4},
    {0xe2015522, 7, 2, 5, 0, 1, 0, 0x0f08d701, 0xe6af6260},
    {0x7cbd1f5a, 7, 1, 6, 0, 1, 2, 0xf2fc1457, 0x18a3831c},
    {0x00000015, 3, 1, 7, 1, 1, 0, 0xa5b4afc4, 0x42c6972e},
    {0x00000015, 4, 1, 5, 1, 1, 0, 0x1120492a, 0x40f771bd},
};
/* Training (4def:4d9e) as the original did it: the character, the hall,
 * free training, creation, the answer and the seed; the hash of the
 * record after and the seed after. */
static const struct { trainee who; uint8_t hall, free, creating; char answer; uint32_t seed;
                      uint32_t hash, after; } trained[] = {
    {{{0, 2, 2, 4, 1, 16, 19, 4001, 11, 3, 20, 0, 0, 0}, {0, 0, 2, 0, 0, 2, 2, 0}}, 1, 0, 0, 'N', 0xa66b0d38, 0x90910556, 0xa66b0d38},
    {{{6, 2, 3, 1, 1, 12, 15, 5000, 43, 0, 30, 0, 1, 0}, {0, 0, 0, 0, 9, 0, 1, 0}}, 1, 0, 0, 'Y', 0xa9964aef, 0xca826370, 0xa9964aef},
    {{{5, 2, 2, 5, 1, 17, 17, 5000, 37, 2, 2, 0, 0, 99}, {0, 0, 2, 0, 0, 2, 2, 0}}, 127, 0, 0, 'Y', 0x0023b682, 0x8d981df1, 0x69d88bfe},
    {{{6, 2, 2, 4, 2, 18, 5, 5000, 54, 5, 18, 0, 0, 50}, {0, 0, 0, 0, 0, 0, 2, 0}}, 127, 1, 0, 'Y', 0x2274ea18, 0xf177b8de, 0xe57f205e},
    {{{6, 2, 1, 1, 2, 18, 9, 400000, 17, 3, 33, 0, 0, 99}, {0, 0, 0, 0, 10, 0, 0, 0}}, 0, 0, 0, 'Y', 0x001edc8e, 0x185b95a0, 0x001edc8e},
    {{{5, 2, 1, 2, 1, 18, 5, 0, 14, 1, 29, 0, 0, 50}, {0, 0, 0, 0, 9, 0, 1, 0}}, 127, 0, 1, 'Y', 0x3e361858, 0xb4c82165, 0x3e361858},
    {{{5, 2, 2, 5, 2, 16, 19, 4001, 60, 3, 12, 0, 1, 0}, {0, 0, 0, 0, 9, 0, 1, 0}}, 1, 0, 1, 'N', 0x8dce6f52, 0x0ac9bad2, 0x8dce6f52},
    {{{3, 2, 2, 5, 1, 18, 3, 2001, 11, 0, 33, 0, 0, 99}, {0, 0, 9, 0, 0, 0, 0, 0}}, 127, 0, 0, 'Y', 0x119b4fe5, 0x46bd0cf8, 0x119b4fe5},
    {{{0, 2, 3, 2, 1, 17, 9, 150000, 26, 5, 34, 4, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1}}, 127, 0, 0, 'Y', 0xf6782941, 0x666b2439, 0xf6782941},
    {{{0, 2, 3, 1, 2, 12, 16, 20000, 13, 0, 5, 0, 1, 0}, {0, 0, 2, 0, 0, 0, 0, 0}}, 0, 0, 1, 'Y', 0x5a11cca5, 0x2c34799c, 0x5a11cca5},
    {{{6, 2, 2, 7, 2, 17, 3, 20000, 24, 1, 40, 0, 0, 50}, {0, 0, 0, 0, 10, 0, 0, 0}}, 0, 0, 0, 'Y', 0x53b3b0ff, 0x68847715, 0x53b3b0ff},
    {{{5, 2, 1, 6, 2, 16, 15, 45000, 19, 4, 21, 0, 0, 0}, {0, 0, 9, 0, 0, 0, 0, 0}}, 8, 0, 0, 'Y', 0xb6125e0c, 0x71f80c38, 0xb6125e0c},
    {{{6, 2, 3, 6, 2, 17, 16, 0, 46, 1, 26, 0, 0, 0}, {0, 0, 0, 0, 9, 0, 1, 0}}, 127, 0, 1, 'Y', 0x1d849e2b, 0x905d1760, 0x1d849e2b},
    {{{0, 2, 3, 6, 2, 12, 15, 5000, 57, 3, 17, 0, 0, 99}, {0, 0, 0, 0, 2, 0, 0, 0}}, 1, 0, 1, 'Y', 0xc751459f, 0xf0576c20, 0xc751459f},
    {{{0, 2, 3, 5, 1, 18, 15, 2001, 31, 5, 25, 0, 1, 99}, {1, 0, 0, 0, 1, 0, 0, 0}}, 127, 0, 0, 'Y', 0x57d53e43, 0xd06397d7, 0x57d53e43},
    {{{6, 2, 1, 6, 1, 16, 15, 45000, 10, 2, 35, 0, 1, 0}, {0, 0, 0, 0, 0, 0, 0, 1}}, 127, 0, 1, 'Y', 0x8ce09658, 0xad20fb29, 0x122cf29e},
    {{{3, 2, 1, 2, 1, 18, 3, 4001, 39, 2, 9, 0, 0, 99}, {1, 0, 0, 0, 0, 0, 0, 0}}, 8, 0, 0, 'Y', 0x8f0be063, 0x50bd4fa8, 0x8f0be063},
    {{{0, 2, 2, 1, 1, 18, 5, 150000, 10, 5, 11, 0, 1, 99}, {0, 0, 2, 0, 0, 0, 0, 0}}, 8, 0, 0, 'Y', 0x56f55245, 0x0859bf0a, 0x741154c3},
    {{{0, 2, 2, 1, 1, 18, 3, 9000, 13, 5, 2, 0, 0, 0}, {0, 0, 2, 0, 0, 2, 2, 0}}, 127, 1, 0, 'N', 0x5af806ef, 0x4d0007de, 0x5af806ef},
    {{{3, 2, 1, 4, 1, 17, 15, 400000, 32, 0, 25, 0, 0, 0}, {0, 0, 0, 0, 9, 0, 1, 0}}, 0, 0, 1, 'Y', 0xdb54e659, 0xd436bbd0, 0xdb54e659},
    {{{5, 2, 1, 3, 2, 16, 15, 5000, 11, 1, 16, 0, 0, 50}, {0, 0, 0, 0, 0, 0, 2, 0}}, 8, 1, 0, 'Y', 0xb47053de, 0x3ae4db55, 0x22b164b4},
    {{{0, 2, 3, 5, 2, 16, 15, 4001, 12, 1, 12, 0, 1, 0}, {1, 0, 0, 0, 1, 0, 0, 0}}, 1, 1, 0, 'Y', 0x18c23ef0, 0xefed15bb, 0x982b008c},
    {{{3, 2, 2, 5, 2, 18, 15, 20000, 5, 5, 3, 0, 0, 99}, {1, 0, 1, 0, 0, 1, 0, 0}}, 8, 0, 0, 'Y', 0x9ad8e8b1, 0x73da5216, 0x6d67e54f},
    {{{0, 2, 1, 3, 1, 18, 15, 400000, 42, 2, 17, 0, 0, 0}, {0, 0, 0, 0, 2, 0, 0, 0}}, 8, 1, 0, 'N', 0xb1de5532, 0x1dded020, 0xb1de5532},
    {{{6, 2, 2, 2, 1, 12, 17, 9000, 35, 0, 1, 0, 0, 0}, {1, 0, 1, 0, 0, 1, 0, 0}}, 127, 0, 0, 'Y', 0x13ab6410, 0xdbd06e63, 0x42ef5fd2},
    {{{6, 2, 1, 3, 2, 17, 17, 400000, 39, 4, 27, 4, 0, 99}, {1, 0, 1, 0, 0, 1, 0, 0}}, 0, 1, 0, 'Y', 0xf0484de3, 0x5251eb00, 0x4b08473d},
    {{{3, 2, 1, 7, 2, 12, 18, 150000, 31, 2, 34, 0, 1, 50}, {1, 0, 1, 0, 0, 1, 0, 0}}, 1, 0, 0, 'N', 0xedf218f0, 0x069e5e92, 0xedf218f0},
    {{{5, 2, 1, 1, 1, 12, 9, 45000, 53, 5, 25, 0, 1, 99}, {0, 0, 9, 0, 0, 0, 0, 0}}, 127, 0, 0, 'Y', 0xe7d22e79, 0x077265e2, 0xe7d22e79},
    {{{3, 2, 2, 7, 1, 12, 5, 150000, 14, 1, 39, 0, 0, 99}, {1, 0, 1, 0, 0, 1, 0, 0}}, 8, 1, 0, 'Y', 0x87318ae1, 0x92d97737, 0xab94022b},
    {{{5, 2, 2, 2, 2, 18, 16, 45000, 28, 5, 34, 0, 1, 0}, {0, 0, 0, 0, 2, 0, 0, 0}}, 127, 0, 0, 'Y', 0x5bf379c4, 0xf83024b4, 0x682a082a},
    {{{0, 2, 3, 6, 1, 17, 15, 5000, 20, 4, 13, 0, 0, 0}, {1, 0, 0, 0, 0, 0, 0, 0}}, 127, 0, 1, 'Y', 0x1a3b72a8, 0xb7a03166, 0x3e08f66e},
    {{{5, 2, 1, 7, 1, 16, 15, 20000, 35, 3, 23, 0, 1, 50}, {1, 0, 0, 0, 1, 0, 0, 0}}, 8, 0, 0, 'Y', 0xcd606cdb, 0x121b37ec, 0xcd606cdb},
    {{{0, 2, 1, 4, 2, 17, 18, 4001, 28, 1, 39, 0, 1, 0}, {0, 0, 2, 0, 0, 2, 2, 0}}, 8, 0, 0, 'Y', 0xe00bad83, 0x982c00ba, 0xacf7edd1},
    {{{0, 2, 1, 4, 2, 18, 16, 0, 9, 2, 14, 4, 0, 50}, {0, 0, 0, 0, 2, 0, 0, 0}}, 8, 0, 0, 'Y', 0xe1b7cc2e, 0x7d69d455, 0xe1b7cc2e},
    {{{5, 2, 3, 6, 2, 17, 5, 20000, 50, 3, 37, 4, 1, 99}, {0, 0, 0, 0, 0, 0, 2, 0}}, 1, 0, 0, 'Y', 0x38a17111, 0x0159bb84, 0x38a17111},
    {{{0, 2, 1, 1, 2, 17, 3, 20000, 48, 3, 38, 0, 0, 50}, {0, 0, 0, 0, 10, 0, 0, 0}}, 8, 0, 0, 'N', 0x2d62c4b0, 0x5eaaf484, 0x2d62c4b0},
    {{{3, 2, 2, 7, 1, 16, 3, 400000, 15, 4, 19, 0, 1, 0}, {0, 0, 0, 0, 2, 0, 0, 0}}, 1, 0, 0, 'Y', 0x55a32d72, 0x30c3f7e6, 0x55a32d72},
    {{{6, 2, 2, 1, 1, 16, 5, 45000, 20, 5, 15, 0, 1, 99}, {1, 0, 0, 0, 0, 0, 0, 0}}, 8, 0, 0, 'Y', 0xac21941d, 0x40a59f7c, 0xac21941d},
    {{{3, 2, 3, 7, 1, 12, 18, 20000, 59, 0, 25, 0, 1, 0}, {0, 0, 2, 0, 0, 2, 2, 0}}, 1, 0, 1, 'Y', 0x256cabc5, 0xf82fc268, 0x93351243},
    {{{6, 2, 3, 4, 1, 17, 15, 45000, 32, 4, 19, 0, 0, 99}, {0, 0, 0, 0, 10, 0, 0, 0}}, 127, 0, 0, 'Y', 0x757143b0, 0x07253b51, 0x757143b0},
    {{{3, 2, 1, 3, 1, 16, 9, 20000, 18, 4, 16, 4, 1, 0}, {0, 0, 0, 0, 9, 0, 1, 0}}, 8, 0, 0, 'Y', 0xeff6475b, 0x52d96305, 0xeff6475b},
    {{{0, 2, 3, 4, 2, 18, 18, 20000, 47, 4, 6, 0, 0, 99}, {0, 0, 9, 0, 0, 0, 0, 0}}, 127, 0, 1, 'Y', 0xc63009a6, 0x0a79a5a5, 0xc63009a6},
    {{{6, 2, 1, 7, 2, 17, 19, 5000, 50, 3, 39, 0, 1, 0}, {0, 0, 2, 0, 0, 0, 0, 0}}, 127, 1, 0, 'N', 0x9e975ee4, 0x02d4ebd3, 0x9e975ee4},
    {{{0, 2, 1, 2, 1, 12, 15, 4001, 57, 1, 6, 0, 1, 0}, {1, 0, 0, 0, 1, 0, 0, 0}}, 8, 0, 1, 'N', 0x4db8581e, 0xf6130b65, 0x4db8581e},
    {{{3, 2, 1, 6, 1, 18, 15, 0, 39, 3, 34, 0, 0, 50}, {0, 0, 0, 0, 0, 0, 2, 0}}, 127, 0, 1, 'Y', 0xff4cf75b, 0xfbfece02, 0xff4cf75b},
    {{{6, 2, 2, 1, 1, 12, 18, 150000, 52, 0, 29, 0, 1, 99}, {0, 0, 9, 0, 0, 0, 0, 0}}, 127, 0, 1, 'Y', 0xb81f6b08, 0x8b1075ae, 0xb81f6b08},
    {{{6, 2, 2, 6, 2, 17, 17, 45000, 40, 5, 6, 0, 0, 0}, {0, 0, 0, 0, 9, 0, 1, 0}}, 8, 0, 0, 'Y', 0x901434a5, 0xac19c422, 0x901434a5},
    {{{5, 2, 2, 7, 2, 12, 5, 4001, 44, 0, 9, 0, 0, 0}, {0, 0, 0, 0, 10, 0, 0, 0}}, 8, 1, 0, 'N', 0x1710d182, 0x4b60b1fc, 0x1710d182},
};
/* The knights' change (4def:567f): the record after. */
static const struct { trainee who; uint32_t hash; } promoted[] = {
    {{{6, 2, 2, 1, 2, 18, 18, 20000, 57, 3, 33, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 4}}, 0xf1ac01d5},
    {{{3, 2, 2, 1, 2, 17, 6, 400000, 6, 2, 22, 0, 1, 50}, {0, 0, 0, 0, 0, 0, 0, 6}}, 0x261a0b46},
    {{{6, 2, 1, 3, 2, 12, 9, 9000, 54, 4, 20, 4, 0, 50}, {0, 0, 0, 0, 0, 0, 0, 7}}, 0x8e20e873},
    {{{0, 2, 2, 6, 1, 12, 11, 5000, 35, 4, 27, 0, 1, 0}, {0, 0, 0, 0, 0, 0, 0, 4}}, 0x2b952bb6},
    {{{6, 2, 1, 6, 1, 16, 12, 400000, 21, 1, 22, 4, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 8}}, 0x9a982112},
    {{{6, 2, 1, 3, 1, 16, 8, 150000, 7, 3, 12, 0, 1, 99}, {0, 0, 0, 0, 0, 0, 0, 6}}, 0x43af3db0},
    {{{3, 2, 2, 1, 1, 18, 4, 45000, 16, 2, 34, 0, 1, 0}, {0, 0, 0, 0, 0, 0, 0, 4}}, 0x74162427},
    {{{6, 2, 2, 4, 1, 18, 16, 45000, 44, 5, 37, 0, 1, 99}, {0, 0, 0, 0, 0, 0, 0, 1}}, 0x0193966d},
    {{{6, 2, 2, 1, 2, 18, 12, 0, 25, 0, 19, 4, 1, 99}, {0, 0, 0, 0, 0, 0, 0, 7}}, 0x67424189},
    {{{6, 2, 2, 7, 2, 16, 10, 0, 35, 5, 7, 4, 0, 50}, {0, 0, 0, 0, 0, 0, 0, 2}}, 0x36bb92c1},
    {{{5, 2, 1, 4, 1, 16, 12, 2001, 9, 0, 34, 0, 0, 99}, {0, 0, 0, 0, 0, 0, 0, 2}}, 0xb23a755a},
    {{{5, 2, 1, 3, 1, 12, 12, 150000, 57, 4, 26, 0, 1, 0}, {0, 0, 0, 0, 0, 0, 0, 4}}, 0xe2dc5634},
    {{{3, 2, 2, 3, 2, 18, 18, 150000, 18, 1, 19, 0, 0, 50}, {0, 0, 0, 0, 0, 0, 0, 1}}, 0xdcc94123},
    {{{6, 2, 1, 6, 1, 16, 13, 0, 46, 0, 20, 0, 1, 99}, {0, 0, 0, 0, 0, 0, 0, 1}}, 0xd8e05503},
    {{{3, 2, 2, 2, 1, 17, 19, 5000, 22, 1, 16, 4, 1, 50}, {0, 0, 0, 0, 0, 0, 0, 8}}, 0x29822035},
    {{{0, 2, 1, 2, 2, 16, 19, 9000, 13, 5, 6, 0, 0, 99}, {0, 0, 0, 0, 0, 0, 0, 2}}, 0x643a9671},
};
/* Modify (4def:28fa) as the original did it: the character, the keys
 * after Pick Character, the hash of the record after. */
static const struct { trainee who; const char *keys; uint32_t hash; } modified[] = {
    {{{5, 0, 3, 6, 1, 16, 3, 2251, 46, 5, 12, 0, 0, 0}, {1, 0, 0, 0, 0, 0, 0, 0}}, "SAaA0ASAK", 0x92ce0d60},
    {{{0, 10, 2, 6, 2, 18, 8, 5000, 27, 4, 17, 0, 0, 99}, {1, 0, 0, 0, 1, 0, 0, 0}}, "SA\x01" "HA\x01" "P\x01" "PE", 0x5e95d29d},
    {{{6, 7, 1, 4, 2, 18, 8, 2251, 60, 0, 15, 0, 1, 99}, {0, 0, 0, 0, 0, 0, 0, 1}}, "ASSS00\x01" "P\x01" "PS\x01" "PAxAK", 0x1c9b58fd},
    {{{3, 4, 3, 5, 1, 18, 11, 2251, 38, 0, 9, 0, 0, 99}, {0, 0, 0, 0, 2, 0, 0, 0}}, "\x01" "PxAASxaSs\x01" "PSK", 0x5d9d3f0f},
    {{{5, 6, 2, 5, 1, 17, 17, 5000, 32, 1, 23, 0, 0, 50}, {0, 0, 0, 0, 0, 0, 2, 0}}, "A\x01" "PSK", 0x3d2d6080},
    {{{3, 15, 3, 4, 2, 12, 9, 2000, 32, 1, 10, 0, 0, 50}, {0, 0, 2, 0, 0, 2, 2, 0}}, "AAA\x01" "HSAK", 0x5ddbb4e7},
    {{{0, 5, 3, 2, 1, 16, 10, 1500, 35, 2, 5, 0, 1, 0}, {0, 0, 0, 0, 0, 2, 0, 0}}, "a\x01" "HAxAxS\x01" "HSAAAK", 0xc3d13cd9},
    {{{5, 10, 1, 3, 2, 16, 11, 5000, 12, 0, 20, 0, 1, 99}, {1, 0, 0, 0, 1, 0, 0, 0}}, "sSAAK", 0xa86beb8a},
    {{{3, 7, 1, 7, 2, 18, 9, 5000, 34, 4, 35, 0, 0, 99}, {0, 0, 0, 0, 0, 0, 0, 1}}, "\x01" "P\x01" "PK", 0x406528e0},
    {{{6, 4, 3, 2, 2, 12, 12, 2000, 51, 0, 14, 0, 0, 0}, {0, 0, 0, 0, 2, 0, 0, 0}}, "AsSAaSASxSaE", 0x0190e405},
    {{{6, 2, 2, 2, 2, 17, 7, 2000, 51, 1, 19, 0, 0, 50}, {0, 0, 2, 0, 0, 0, 0, 0}}, "ASS\x01" "Hs\x01" "HxA\x01" "HSAs\x01" "HK", 0x2d88f504},
    {{{6, 0, 3, 4, 2, 18, 19, 2251, 10, 2, 14, 0, 0, 50}, {1, 0, 0, 0, 0, 0, 0, 0}}, "SAA0AE", 0x23e12ab2},
    {{{6, 9, 2, 2, 1, 16, 18, 1251, 8, 1, 28, 0, 0, 99}, {1, 0, 1, 0, 0, 1, 0, 0}}, "AA\x01" "PAAA0\x01" "HSSK", 0x6590782a},
    {{{6, 5, 1, 7, 2, 16, 7, 5000, 11, 0, 32, 0, 0, 0}, {0, 0, 0, 0, 0, 2, 0, 0}}, "\x01" "HASASA0SAAAAAK", 0x1f72b87b},
    {{{0, 5, 1, 6, 2, 12, 15, 1500, 36, 3, 36, 0, 0, 99}, {0, 0, 0, 0, 0, 2, 0, 0}}, "AA\x01" "HSAsS0SSAA\x01" "HAK", 0xace52dec},
    {{{3, 15, 3, 2, 2, 16, 4, 2001, 8, 3, 26, 0, 0, 99}, {0, 0, 2, 0, 0, 2, 2, 0}}, "s\x01" "HK", 0xb75961bf},
    {{{6, 10, 2, 1, 2, 18, 10, 2000, 39, 4, 33, 0, 0, 99}, {1, 0, 0, 0, 1, 0, 0, 0}}, "Axs\x01" "HaA\x01" "PSa\x01" "HA0AE", 0x2060f330},
    {{{6, 6, 2, 7, 1, 12, 9, 5000, 38, 5, 12, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 2, 0}}, "sSSaa\x01" "P\x01" "H\x01" "PAAAA\x01" "PK", 0x7dc5f9c3},
    {{{3, 2, 1, 5, 2, 12, 7, 1251, 26, 1, 29, 0, 1, 50}, {0, 0, 2, 0, 0, 0, 0, 0}}, "sAK", 0x1184f346},
    {{{6, 7, 3, 7, 1, 18, 16, 1251, 34, 4, 2, 0, 1, 50}, {0, 0, 0, 0, 0, 0, 0, 1}}, "A\x01" "HSA\x01" "HSA\x01" "P\x01" "HAA\x01" "PA\x01" "HK", 0xc7fa464f},
    {{{0, 4, 3, 6, 2, 12, 16, 2251, 6, 5, 11, 0, 1, 0}, {0, 0, 0, 0, 2, 0, 0, 0}}, "asK", 0xded8da46},
    {{{6, 7, 1, 7, 2, 18, 13, 1251, 60, 5, 33, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1}}, "ASxAK", 0xc5ead2a4},
    {{{5, 6, 1, 5, 1, 16, 16, 1251, 60, 3, 29, 0, 1, 50}, {0, 0, 0, 0, 0, 0, 2, 0}}, "sAAAASK", 0x2cbd2c30},
    {{{6, 6, 3, 5, 2, 12, 16, 2500, 31, 3, 27, 0, 1, 0}, {0, 0, 0, 0, 0, 0, 2, 0}}, "As\x01" "P\x01" "PS0A\x01" "HsAE", 0xbc76182b},
    {{{3, 6, 3, 7, 1, 16, 17, 2251, 31, 3, 34, 0, 1, 0}, {0, 0, 0, 0, 0, 0, 2, 0}}, "AS\x01" "HS0AAAaSxASK", 0x1c22091a},
    {{{3, 15, 2, 4, 1, 12, 8, 2001, 20, 0, 1, 0, 0, 99}, {0, 0, 2, 0, 0, 2, 2, 0}}, "\x01" "PSA\x01" "PSK", 0xb1c1b6e9},
    {{{3, 2, 2, 1, 2, 17, 8, 5000, 35, 4, 5, 0, 0, 50}, {0, 0, 2, 0, 0, 0, 0, 0}}, "\x01" "Ps\x01" "HSAxAK", 0x9d47e6a7},
    {{{0, 9, 2, 3, 2, 16, 12, 2251, 53, 3, 27, 0, 0, 50}, {1, 0, 1, 0, 0, 1, 0, 0}}, "A\x01" "H0E", 0x1665d5b2},
    {{{6, 6, 3, 4, 1, 16, 12, 2000, 18, 0, 1, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 2, 0}}, "S0AAsSK", 0x266945b8},
    {{{6, 10, 1, 2, 1, 12, 15, 2500, 38, 0, 3, 0, 0, 50}, {1, 0, 0, 0, 1, 0, 0, 0}}, "A\x01" "HAASSsE", 0xc3fc7a45},
    {{{6, 0, 1, 1, 1, 17, 7, 1500, 35, 5, 40, 0, 1, 99}, {1, 0, 0, 0, 0, 0, 0, 0}}, "SAA\x01" "HAASK", 0x18ff416a},
    {{{0, 5, 1, 1, 2, 16, 3, 2500, 49, 4, 38, 0, 1, 50}, {0, 0, 0, 0, 0, 2, 0, 0}}, "SxAAxK", 0xe6476a9c},
    {{{5, 6, 1, 1, 1, 12, 19, 2001, 48, 1, 27, 0, 1, 50}, {0, 0, 0, 0, 0, 0, 2, 0}}, "AA0sSS\x01" "H0xAE", 0xeffc4c4c},
    {{{3, 7, 2, 2, 2, 16, 13, 2500, 46, 2, 13, 0, 1, 0}, {0, 0, 0, 0, 0, 0, 0, 1}}, "As0s\x01" "HA\x01" "HAAK", 0xfd24b1e5},
    {{{6, 15, 2, 4, 2, 12, 11, 1251, 18, 2, 32, 0, 1, 50}, {0, 0, 2, 0, 0, 2, 2, 0}}, "\x01" "HA0aK", 0x5fe5d7ca},
    {{{0, 10, 3, 5, 2, 16, 10, 1500, 33, 4, 8, 0, 0, 50}, {1, 0, 0, 0, 1, 0, 0, 0}}, "S\x01" "HA\x01" "PAAsK", 0xbd866c5b},
    {{{6, 7, 3, 6, 1, 18, 14, 2251, 59, 4, 22, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1}}, "AAAAAA\x01" "HSa0S\x01" "P\x01" "PK", 0x699a4426},
    {{{5, 4, 3, 1, 2, 18, 10, 2251, 20, 2, 4, 0, 1, 99}, {0, 0, 0, 0, 2, 0, 0, 0}}, "SASxaAAAASaA\x01" "H\x01" "HK", 0xa6044517},
    {{{3, 9, 3, 6, 2, 17, 18, 1251, 26, 0, 17, 0, 0, 99}, {1, 0, 1, 0, 0, 1, 0, 0}}, "aAAA\x01" "HK", 0xcff0fdf1},
    {{{5, 15, 1, 5, 1, 12, 18, 2500, 56, 1, 3, 0, 0, 99}, {0, 0, 2, 0, 0, 2, 2, 0}}, "\x01" "P\x01" "PASAAS0SE", 0x66cf8a94},
};
/* The icon editor (4def:408b) as the original ran it: head, body, size,
 * colours, keys; the hash of +0x135-+0x13e after. */
static const struct { uint8_t head, body, size, colours[6]; const char *keys;
                      uint32_t hash; } edited[] = {
    {8, 26, 2, {0xbb, 0x94, 0x59, 0x8e, 0x38, 0x0d}, "2PSKBEHKP2\x1b" "\x1b" "\x1b" "Y", 0x9b233f7b},
    {2, 3, 1, {0x64, 0x4d, 0x6b, 0xa9, 0x3f, 0x23}, "SHBSHWNKSBEP\x1b" "\x1b" "\x1b" "Y", 0xf312119d},
    {6, 23, 1, {0xb5, 0x19, 0xf4, 0xbf, 0x02, 0xcf}, "WANPB1PKHK\x1b" "\x1b" "\x1b" "Y", 0xa21eaac0},
    {3, 8, 2, {0x9d, 0xb2, 0xa3, 0x57, 0xaa, 0x30}, "FKEPPE1PLPKSHPPNKKHPN\x1b" "\x1b" "\x1b" "Y", 0x8e7808f2},
    {7, 1, 1, {0xce, 0xf8, 0xc8, 0x5d, 0x64, 0x80}, "KWNPAKE\x1b" "\x1b" "\x1b" "Y", 0x47c9ca4e},
    {12, 20, 1, {0x52, 0xd6, 0x2f, 0x9f, 0x80, 0xce}, "HAHAHPPAKFNKPPSF2S\x1b" "\x1b" "\x1b" "Y", 0x44c412f7},
    {3, 26, 2, {0x27, 0xeb, 0x6f, 0x75, 0xf7, 0xa2}, "1WPPFFBSP\x1b" "\x1b" "\x1b" "Y", 0xfe4416c8},
    {13, 21, 1, {0x76, 0xa0, 0xbc, 0x58, 0x6c, 0x9b}, "1LFHBWBPNFNAENLL\x1b" "\x1b" "\x1b" "Y", 0x2919a3ea},
    {7, 8, 2, {0xe7, 0x06, 0x96, 0x86, 0x5e, 0x04}, "EEENPKWBKNPPWSNKW\x1b" "\x1b" "\x1b" "Y", 0x61644cd9},
    {3, 6, 1, {0xe9, 0x71, 0x0f, 0xcc, 0x3e, 0xdb}, "PAPN\x1b" "\x1b" "\x1b" "Y", 0xf4cffd04},
    {8, 16, 2, {0xb6, 0xb1, 0xd5, 0xcf, 0x03, 0xad}, "EPPHP\x1b" "\x1b" "\x1b" "Y", 0x45e21b17},
    {13, 18, 2, {0xa4, 0x3c, 0xab, 0x41, 0x7c, 0xfd}, "LWKPHKSKKNS1\x1b" "\x1b" "\x1b" "Y", 0x3d579b5e},
    {2, 13, 1, {0x96, 0x2a, 0xdf, 0xe0, 0xc6, 0x4e}, "ANNFAESKSHWPNNNEPSAEAELE\x1b" "\x1b" "\x1b" "Y", 0xa8887211},
    {8, 8, 2, {0x7c, 0xfd, 0x80, 0x61, 0xbe, 0x03}, "WESHSHB2PNHKHKEELENNPKK\x1b" "\x1b" "\x1b" "Y", 0xf40204ed},
    {6, 20, 1, {0xdf, 0x35, 0x83, 0x79, 0x6e, 0x6d}, "1PS2KHK2HNEPPFFP1KHS\x1b" "\x1b" "\x1b" "Y", 0xa71bcd40},
    {6, 27, 2, {0x9a, 0xe6, 0x53, 0xd4, 0x6d, 0x60}, "1PNNPEH2H\x1b" "\x1b" "\x1b" "Y", 0x05bd31d3},
    {7, 12, 2, {0x40, 0xf7, 0xe2, 0x2c, 0x43, 0x78}, "E2PK22SENPE2NNSP\x1b" "\x1b" "\x1b" "Y", 0xc6b3a61d},
    {7, 6, 2, {0xf6, 0x32, 0x5e, 0xfe, 0x0f, 0x87}, "NNNKNPWKLPHKWHKAKH\x1b" "\x1b" "\x1b" "Y", 0x14ec52f5},
    {6, 16, 1, {0x0f, 0x91, 0x24, 0x1c, 0x8c, 0x96}, "1SNEBWP11SLEES1PPWW1BN\x1b" "\x1b" "\x1b" "Y", 0xc4d3699f},
    {13, 24, 1, {0x28, 0x14, 0xc8, 0x38, 0xec, 0x49}, "NALPKKPFWEWNSPKHKKWEAPS\x1b" "\x1b" "\x1b" "Y", 0x5f52ac71},
    {1, 24, 1, {0xec, 0x35, 0xa7, 0xa5, 0xeb, 0x1d}, "HAHLKWWPFKNEHKEKPN\x1b" "\x1b" "\x1b" "Y", 0xb8d342c9},
    {13, 12, 2, {0xd6, 0x2b, 0x37, 0xc3, 0x1c, 0xfa}, "SSEKKFPFBP2WNEPPEPK\x1b" "\x1b" "\x1b" "Y", 0x552b1960},
    {5, 30, 2, {0x6c, 0x32, 0x68, 0xef, 0x6a, 0xe1}, "1SWPNWPPKPKN1S2N\x1b" "\x1b" "\x1b" "Y", 0x7d8d1f01},
    {0, 0, 1, {0xa5, 0xa4, 0x58, 0xb7, 0xa2, 0x7a}, "FLLWNPNLBNN\x1b" "\x1b" "\x1b" "Y", 0x68185627},
};

/* More edges, one a quirk each. */
static void test_edges(void)
{
    uint8_t c[COK_CHARACTER_SIZE] = {0}, most;
    /* The least is at least 1: a mage of constitution 3. */
    c[0xfe] = 1;
    c[0x19] = 3;
    CHECK(cok_modify_least(c) == 1);
    /* Past its top level a class sets the most: a fighter of level 12 has
     * 3 x 3 + 90. */
    memset(c, 0, sizeof c);
    c[0xfb] = 12;
    c[0x19] = 10;
    CHECK(cok_modify_most(c, c, &most) && most == 99);
    /* The default body of a knight. */
    memset(c, 0, sizeof c);
    c[0x100] = 1;
    cok_create_icon(c);
    CHECK(c[0x136] == 0x18);
    empty_party();
    /* A cleric of deity 4 trains by the first row: 3000 is not enough. */
    cok_character *cl = add(member("CLER", 0, 1));
    cl->record[0x5d] = 4;
    put32(cl->record + 0x116, 3000);
    game.vm.mem7c00[0x2a8] = 127;
    keys("S");
    cok_train(&game);
    CHECK(logged("Not Enough Experience") && cl->record[0xf9] == 1);
    /* A ranger above level 8 learns a spell when another class rises. */
    empty_party();
    cok_character *rt = add(member("RANGE", 4, 13));
    rt->record[0xff] = 1;
    put32(rt->record + 0x116, 2000);
    CHECK(cok_character_levels(rt, &game.item_types, game.error, sizeof game.error));
    keys("SYL");
    cok_train(&game);
    CHECK(rt->record[0xff] == 2 && logged("list: Spells to Choose") && s.at == s.length);
    /* Modify: wisdom sets a cleric's first spells a day to 1, and Exit
     * leaves it. */
    empty_party();
    cok_character *w = add(member("WIS", 0, 1));
    w->record[0x5b] = 0;
    w->record[0x11c] = 3;
    put32(w->record + 0x116, 1500);
    keys("S\x01P\x01PAE");
    cok_modify(&game);
    CHECK(w->record[0x11c] == 1 && w->record[0x15] == 12);
    /* The icon: Prev from body 0 is 31; the weapon's colour is the sixth
     * byte. */
    w->record[0x135] = 5;
    w->record[0x136] = 0;
    w->record[0x138] = 2;
    memset(w->record + 0x139, 0x11, 6);
    keys("PWPKE1WNKEEY");
    CHECK(cok_icon_edit(&game));
    CHECK(w->record[0x136] == 31 && w->record[0x13e] == 0x12 && w->record[0x139] == 0x11);
    /* Begin draws the frame after a game was loaded here when PIC 9 is the
     * small picture. */
    game.vm.keep_vars = true;
    game.vm.mem4b00[0xf2] = 17;
    game.vm.mem4b00[0x138] = 0;
    game.picture_id = 9;
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
    keys("");
    CHECK(cok_start_command(&game, 'B') && !cell_black(16, 8));
    cok_picture_fill(&game.screen, 0, 0, 40, 200, 0);
    game.picture_id = COK_ADVENTURE_NO_PICTURE;
    CHECK(cok_start_command(&game, 'B') && cell_black(16, 8));
    game.vm.keep_vars = false;
    game.vm.mem4b00[0xf2] = 0;
    empty_party();
}

/* Boundary cases of the quirks the vectors may miss. */
static void test_quirks(void)
{
    uint8_t c[COK_CHARACTER_SIZE] = {0};
    /* 4def:4789: 19-20 three more, 21-23 four. */
    c[0x19] = 20;
    CHECK(cok_train_constitution(c, 4) == 5);
    c[0x19] = 21;
    CHECK(cok_train_constitution(c, 4) == 6 && cok_train_constitution(c, 6) == 2);
    /* 4def:4b3a with nothing trained still gives a hit point. */
    memset(c, 0, sizeof c);
    c[0xf9] = 1;
    CHECK(cok_train_hit_points(&game, c, 0, 1) && c[0x11b] == 1 && c[0x62] == 1);
    empty_party();
    /* A kender fighter stops at 6 with strength 17, but not below it. */
    cok_character *k = add(member("KEN", 2, 6));
    k->record[0x5a] = 5;
    k->record[0x10] = 16;
    put32(k->record + 0x116, 75000);
    game.vm.mem7c00[0x2a8] = 127;
    keys("SY");
    cok_train(&game);
    CHECK(k->record[0xfb] == 7);
    k->record[0xfb] = 6;
    k->record[0x10] = 17;
    keys("S");
    cok_train(&game);
    CHECK(k->record[0xfb] == 6 && logged("Not Enough Experience"));
    /* +0xd8 and +0xd9: a level takes the share. */
    k->record[0x10] = 16;
    k->record[0xd8] = 1;
    k->record[0xd9] = 10;
    keys("SY");
    cok_train(&game);
    CHECK(k->record[0xd8] == 0 && k->record[0xd9] == 0);
    empty_party();
    /* The Sword needs knight level 3. */
    cok_character *n = add(member("SIR N", 7, 2));
    n->record[0x5c] = 1;
    put32(n->record + 0x116, 12000);
    for (size_t i = 0; i < 6; ++i) n->record[0x10 + 2 * i] = 15;
    keys("SE");
    cok_train_knight(&game);
    CHECK(logged("is too inexperienced.;") && n->record[0x5c] == 1);
    empty_party();
    /* Modify: the experience's high word, and 5000. */
    cok_character *m = add(member("MO", 5, 2));
    m->record[0x5b] = 5;
    put32(m->record + 0x116, 0x10000 + 2000);
    keys("S");
    cok_modify(&game);
    CHECK(logged("can't be modified"));
    put32(m->record + 0x116, 5000);
    keys("SE");
    cok_modify(&game);
    CHECK(!logged("can't be modified"));
    empty_party();
    /* The start menu turns free training off as it opens, so its rows
     * never offer Train for it. */
    add(member("A", 2, 1));
    game.vm.mem7c00[0x2a8] = 0;
    game.helm = true;
    keys("");
    cok_start_command(&game, 'J');
    game.helm = false;
    CHECK(game.free_training);
    game.vm.mode = 4;
    keys("\x1b");
    cok_start_menu(&game);
    CHECK(!logged("item: Train Character;") && !game.free_training);
    empty_party();
}

/* The roster's rules, each at its edge. */
static void test_roster_rules(void)
{
    char path[128], rules[96];
    snprintf(rules, sizeof rules, "%s/rules", dir);
    CHECK(mkdir(rules, 0755) == 0);
    snprintf(game.save_dir, sizeof game.save_dir, "%s", rules);
    empty_party();
    /* Six player characters at most: with five, R00 joins; with six, the
     * next does not, and nothing says why. */
    static const char *const names[6] = {"P1", "P2", "P3", "P4", "P5", "P6"};
    for (size_t i = 0; i < 5; ++i) add(member(names[i], 2, 1));
    cok_character *r = member("ROSTA", 2, 1);
    char error[300];
    CHECK(cok_character_write_file(r, rules, "R00", "WHO", error, sizeof error));
    cok_character *q = member("ROSTB", 2, 1);
    CHECK(cok_character_write_file(q, rules, "R01", "WHO", error, sizeof error));
    keys("AA");
    cok_roster_add(&game);
    CHECK(game.party.count == 6 && logged("party: ROSTA joins;") && s.at == 1);
    /* Add ends with six; the next is turned away saying nothing. */
    keys("A");
    cok_roster_add(&game);
    CHECK(game.party.count == 6 && logged("item: ROSTB") && !logged("print:"));
    /* NPCs count against the eight records instead. */
    empty_party();
    for (size_t i = 0; i < 2; ++i) add(member(names[i], 2, 1));
    q->record[0xe7] = 0x80;
    CHECK(cok_character_write_file(q, rules, "R01", "WHO", error, sizeof error));
    keys("\x01PA\x1b");
    cok_roster_add(&game);
    CHECK(logged("party: ROSTB joins;"));
    /* Three rangers at most; a ranger's level byte need only be set. */
    empty_party();
    for (size_t i = 0; i < 3; ++i) add(member(names[i], 4, 1));
    r->record[0xfd] = 1;
    CHECK(cok_character_write_file(r, rules, "R00", "WHO", error, sizeof error));
    keys("A\x1b");
    cok_roster_add(&game);
    CHECK(logged("print: too many rangers in party;") && game.party.count == 3);
    /* Evil is (alignment + 1) % 3 == 0 as signed bytes: alignment 0xff
     * (-1) counts too; a paladin will tolerate none. */
    empty_party();
    cok_character *e = add(member("WEIRD", 2, 1));
    e->record[0x10a] = 0xff;
    r->record[0xfd] = 0;
    r->record[0xfc] = 1;
    CHECK(cok_character_write_file(r, rules, "R00", "WHO", error, sizeof error));
    keys("A\x1b");
    cok_roster_add(&game);
    CHECK(logged("paladins do not join with evil scum") && game.party.count == 1);
    /* That message alone waits twice. */
    CHECK(s.waits == 2);
    /* The jewelry's low byte at 0x80 leaves a character out of the list. */
    r->record[0xfc] = 0;
    r->record[0xf7] = 0x80;
    CHECK(cok_character_write_file(r, rules, "R00", "WHO", error, sizeof error));
    keys("\x1b");
    cok_roster_add(&game);
    CHECK(!logged("ROSTA") && logged("ROSTB"));
    /* From an empty party, Add ends once six player characters are in. */
    empty_party();
    r->record[0xf7] = 0;
    CHECK(cok_character_write_file(r, rules, "R00", "WHO", error, sizeof error));
    static const char *const more[6] = {"M1", "M2", "M3", "M4", "M5", "M6"};
    for (size_t i = 0; i < 6; ++i) {
        cok_character *m = member(more[i], 2, 1);
        char base[8];
        snprintf(base, sizeof base, "R%02zu", i + 2);
        CHECK(cok_character_write_file(m, rules, base, "WHO", error, sizeof error));
        cok_character_free(m);
        free(m);
    }
    q->record[0xe7] = 0;
    CHECK(cok_character_write_file(q, rules, "R01", "WHO", error, sizeof error));
    keys("A\x01PA\x01PA\x01PA\x01PA\x01PA\x01PA\x01PA");
    cok_roster_add(&game);
    CHECK(game.party.count == 6 && s.at == 16);
    /* The same name and +0x115 twice in the roster: the second is a
     * duplicate once the first has joined. */
    empty_party();
    add(member("ZZ", 2, 1));
    char twins[128];
    snprintf(twins, sizeof twins, "%s/twins", dir);
    CHECK(mkdir(twins, 0755) == 0);
    snprintf(game.save_dir, sizeof game.save_dir, "%s", twins);
    cok_character *d = member("TWIN", 2, 1);
    CHECK(cok_character_write_file(d, twins, "T1", "WHO", error, sizeof error));
    CHECK(cok_character_write_file(d, twins, "T2", "WHO", error, sizeof error));
    keys("A\x01PA\x1b");
    cok_roster_add(&game);
    CHECK(game.party.count == 2 && logged("party: TWIN joins;"));
    snprintf(game.save_dir, sizeof game.save_dir, "%s", rules);
    cok_character_free(d);
    free(d);
    /* Loading a game erases the roster copies of its characters. */
    empty_party();
    add(member("ROSTA", 2, 1));
    CHECK(cok_camp_save_game(&game, 'C'));
    snprintf(path, sizeof path, "%s/ROSTA.WHO", rules);
    CHECK(cok_character_write_file(r, rules, "ROSTA", "WHO", error, sizeof error));
    empty_party();
    keys("C");
    CHECK(cok_roster_load(&game) && access(path, F_OK) != 0 && game.party.count == 1);
    for (size_t i = 0; i < 8; ++i) {
        snprintf(path, sizeof path, "%s/R%02zu.WHO", rules, i);
        remove(path);
    }
    cok_character_free(r);
    free(r);
    cok_character_free(q);
    free(q);
    empty_party();
    snprintf(game.save_dir, sizeof game.save_dir, "%s", dir);
}

/* The demonstration: no waiting, no small picture on CLEAR BOX; PROGRAM 0
 * redraws outside 3D areas. */
static void test_demo(void)
{
    cok_keyboard k = cok_adventure_keyboard(&game);
    (void)k;
    game.demo = true;
    keys("");
    cok_adventure_prompt_key(&game, "press <enter>/<return> to continue");
    CHECK(!game.input_ended);
    keys("\x02x");
    cok_adventure_prompt_key(&game, "press <enter>/<return> to continue");
    CHECK(s.at == s.length && !game.input_ended);
    /* The combat panel's text pages without waiting too; a menu waits for a
     * key (67b5:03e2 reads only once one is pressed). */
    keys("");
    cok_character *c = member("TALKER", 2, 1);
    uint8_t mode = game.vm.mode;
    game.vm.mode = 5;
    CHECK(cok_arena_say(&game, c, "speaks at great length of many things in a narrow window",
                        20, false));
    CHECK(!game.input_ended && !game.vm.abort && s.at == 0);
    CHECK(cok_adventure_wait_key(&game) == 0 && !game.input_ended);
    bool special;
    CHECK(cok_camp_menu(&game, "Go on: ", "Yes No", false, false, &special) < 0 &&
          game.input_ended);
    game.vm.mode = mode;
    cok_character_free(c);
    free(c);
    game.demo = false;
    keys("");
    cok_adventure_prompt_key(&game, "press <enter>/<return> to continue");
    CHECK(game.input_ended);
    keys("");
}

/* The tables the start menu's modules embed, against build/START_FULL.EXE. */
static void test_tables(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("start: build/START_FULL.EXE not built; tables not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + 0xbc6 * 16;
    extern const cok_ds_table cok_train_tables[], cok_create_tables[];
    extern const size_t cok_train_table_count, cok_create_table_count;
    const cok_ds_table *lists[2] = {cok_train_tables, cok_create_tables};
    size_t counts[2] = {cok_train_table_count, cok_create_table_count};
    for (size_t l = 0; l < 2; ++l)
        for (size_t i = 0; i < counts[l]; ++i)
            for (size_t k = 0; k < lists[l][i].size; ++k)
                CHECK(exe[ds + lists[l][i].offset + k * lists[l][i].stride] == lists[l][i].bytes[k]);
    /* The start menu's items, string[40]s 0x2a bytes apart from DS:0878. */
    for (unsigned i = 0; i < 13; ++i) {
        const char *item = cok_start_item(i);
        const uint8_t *p = exe + ds + 0x878 + 0x2a * i;
        CHECK(p[0] == strlen(item) && memcmp(p + 1, item, p[0]) == 0);
    }
    CHECK(cok_start_item(13) == NULL);
    (void)size;
}

/* 6346:371e, the experience table and 4def:4789, 257c. */
static void test_experience(void)
{
    uint8_t c[COK_CHARACTER_SIZE] = {0}, row = 0xaa;
    CHECK(cok_train_row(c, 0, &row) && row == 0);
    c[0x5d] = 5;
    CHECK(cok_train_row(c, 0, &row) && row == 1);
    CHECK(cok_train_row(c, 2, &row) && row == 2 && cok_train_row(c, 4, &row) && row == 6);
    c[0x5e] = 1;
    CHECK(cok_train_row(c, 5, &row) && row == 7);
    c[0x5e] = 2;
    CHECK(cok_train_row(c, 5, &row) && row == 8 && cok_train_row(c, 6, &row) && row == 9);
    /* A druid, a paladin and a knight of no order are the uninitialized local. */
    row = 0xaa;
    CHECK(!cok_train_row(c, 1, &row) && !cok_train_row(c, 3, &row) && !cok_train_row(c, 7, &row));
    CHECK(row == 0xaa);
    c[0x5c] = 3;
    CHECK(cok_train_row(c, 7, &row) && row == 5);
    int32_t need;
    CHECK(cok_train_experience(2, 2, &need) && need == 2001);
    CHECK(cok_train_experience(2, 3, &need) && need == 4001);
    CHECK(cok_train_experience(6, 8, &need) && need == -1); /* rangers stop at 7 */
    CHECK(cok_train_experience(9, 9, &need) && need == 110001);
    CHECK(cok_train_experience(9, 10, &need) && need == -1);
    /* 4def:4789: the warriors' extra at 17-25. */
    c[0x19] = 18;
    CHECK(cok_train_constitution(c, 2) == 4 && cok_train_constitution(c, 5) == 2);
    c[0x19] = 3;
    CHECK(cok_train_constitution(c, 2) == -2);
    int8_t b;
    c[0x19] = 19;
    CHECK(cok_train_hit_die(c, 7, &b) && b == 5 && cok_train_hit_die(c, 0, &b) && b == 2);
    c[0x19] = 20;
    CHECK(!cok_train_hit_die(c, 2, &b));
}

/* 4def:4b3a: the better of two rolls, the bonus times the dice, divided by
 * the classes, the bonus unsigned. */
static void test_hit_points(void)
{
    uint8_t c[COK_CHARACTER_SIZE] = {0};
    c[0xfb] = 2; /* fighter 2: one d10 */
    c[0x19] = 16;
    c[0x62] = 20;
    c[0x197] = 15;
    game.vm.seed = 77;
    uint32_t seed = 77;
    uint8_t a = cok_dice(&seed, 1, 10), d = cok_dice(&seed, 1, 10);
    CHECK(cok_train_hit_points(&game, c, 0xff, 1));
    uint8_t gained = (uint8_t)((a > d ? a : d) + 2);
    CHECK(c[0x62] == 20 + gained && c[0x197] == 15 + gained && game.vm.seed == seed);
    CHECK(c[0x11b] == (a > d ? a : d));
    /* Two classes at constitution 3: -2 each, -4 divided as the byte 252
     * by 2, 126 more. */
    memset(c, 0, sizeof c);
    c[0xf9] = 2;
    c[0xfb] = 2;
    c[0x19] = 3;
    seed = game.vm.seed = 5;
    uint8_t r1 = cok_dice(&seed, 1, 8), r2 = cok_dice(&seed, 1, 8), r3 = cok_dice(&seed, 1, 10),
            r4 = cok_dice(&seed, 1, 10);
    uint8_t sum = (uint8_t)((r1 > r2 ? r1 : r2) + (r3 > r4 ? r3 : r4));
    CHECK(cok_train_hit_points(&game, c, 0xff, 2));
    CHECK(c[0x62] == (uint8_t)(126 + sum / 2) && c[0x11b] == sum / 2);
    /* At the top level a fixed amount replaces what came before. */
    memset(c, 0, sizeof c);
    c[0xf9] = 3;
    c[0xfb] = 10; /* the fighter's top */
    c[0x19] = 7;
    CHECK(cok_train_hit_points(&game, c, 0xff, 2) && c[0x11b] == 1 && c[0x62] == 1);
    CHECK(!cok_train_hit_points(&game, c, 0xff, 0) && game.vm.status == COK_ECL_DIVIDE_BY_ZERO);
    game.vm.status = COK_ECL_OK;
}

/* Training from the start menu (4def:4d9e). */
static void test_train(void)
{
    empty_party();
    cok_character *f = add(member("FIGHTY", 2, 2));
    uint8_t *c = f->record;
    game.vm.mem7c00[0x2a8] = 127;
    /* Not enough experience. */
    put32(c + 0x116, 3000);
    keys("S");
    cok_train(&game);
    CHECK(logged("Not Enough Experience") && c[0xfb] == 2);
    /* Experience past the second next level is cut, before the question,
     * which No leaves at that. */
    put32(c + 0x116, 20000);
    keys("SN");
    cok_train(&game);
    CHECK(get32(c + 0x116) == 8000 && c[0xfb] == 2);
    CHECK(logged("print:     a level 3 Fighter;"));
    keys("SY");
    cok_train(&game);
    CHECK(c[0xfb] == 3 && logged("print: Congratulations...") && c[0x62] > 10);
    /* A hall of other classes. */
    game.vm.mem7c00[0x2a8] = 1;
    keys("S");
    cok_train(&game);
    CHECK(logged("We don't train that class here"));
    /* The unconscious are not trained; with free training they are, their
     * experience raised to what the level needs. */
    game.vm.mem7c00[0x2a8] = 127;
    c[0x188] = 4;
    keys("S");
    cok_train(&game);
    CHECK(logged("we only train conscious people"));
    game.free_training = true;
    put32(c + 0x116, 0);
    keys("SY");
    cok_train(&game);
    CHECK(c[0xfb] == 4 && get32(c + 0x116) == 8001);
    game.free_training = false;
    c[0x188] = 0;
    /* A druid level first reads row 0x74, the combatants' pointers, which
     * Pick Character leaves in 6346:371e's local: never trainable, and a
     * stop with free training. After a cleric it takes the cleric's row. */
    c[0xfa] = 1;
    c[0xfb] = 0;
    keys("S");
    cok_train(&game);
    CHECK(game.vm.status == COK_ECL_OK && logged("Not Enough Experience"));
    put32(c + 0x116, 0x20000000);
    keys("S");
    cok_train(&game);
    CHECK(game.vm.status == COK_ECL_UNDEFINED);
    put32(c + 0x116, 8001);
    game.free_training = true;
    keys("S");
    cok_train(&game);
    CHECK(game.vm.status == COK_ECL_UNDEFINED);
    game.free_training = false;
    c[0xf9] = 1;
    put32(c + 0x116, 20000);
    keys("SN");
    cok_train(&game);
    /* Both rise; their bits, DS:38f2's 2 each, add up to the thief's 4,
     * so neither line is drawn. */
    CHECK(game.vm.status == COK_ECL_OK && logged("Do you wish to train? ") &&
          !logged("a level 2"));
    c[0xf9] = 0;
    put32(c + 0x116, 8001);
    keys("");
    c[0xfa] = 0;
    c[0xfb] = 2;
    /* A mage rising learns a spell; Escape shows the list again. */
    cok_character *m = add(member("MAGEY", 5, 1));
    m->record[0x5e] = 1;
    put32(m->record + 0x116, 5000);
    CHECK(cok_character_levels(m, &game.item_types, game.error, sizeof game.error));
    game.vm.character = m->record;
    keys("SY\x1bL");
    cok_train(&game);
    CHECK(m->record[0xfe] == 2 && logged("list: Spells to Choose") && logged("choice: "));
    CHECK(s.at == s.length);
    /* Creation's silent training: one level for each class that has the
     * experience, and a mage's spells by its new level. */
    cok_character *n = add(member("NEWBIE", 5, 1));
    n->record[0x5e] = 1;
    put32(n->record + 0x116, 5000);
    game.vm.mem7c00[0x2a8] = 0xff;
    keys("");
    CHECK(cok_train_silently(&game, n) && n->record[0xfe] == 2 && n->record[0x62 + 15] == 1);
    CHECK(!cok_train_silently(&game, n) && n->record[0xfe] == 2);
    CHECK(s.log[0] == '\0');
    /* The unconscious are refused with the notice even then (4def:4dcb). */
    n->record[0x188] = 4;
    keys("");
    cok_train_silently(&game, n);
    CHECK(logged("print: we only train conscious people;"));
    n->record[0x188] = 0;
    empty_party();
}

/* Knight Change Classes (4def:5812, 567f). */
static void test_knight(void)
{
    empty_party();
    cok_character *k = add(member("SIR K", 7, 3));
    uint8_t *c = k->record;
    c[0x5c] = 1;
    c[0x19] = 15;
    put32(c + 0x116, 12000);
    for (size_t i = 0; i < 6; ++i) c[0x10 + 2 * i] = 15;
    keys("SC");
    cok_train_knight(&game);
    CHECK(logged("may become a") && c[0x5c] == 2 && c[0x100] == 3);
    /* Too inexperienced for the Rose, but F9 (scan 0x43, C) changes all the
     * same, losing a level and its hit points: the Rose's level 3 needs
     * 12000, which it has; at level 4, 27000. */
    c[0x100] = 4;
    c[0x11b] = 40;
    c[0x62] = c[0x197] = 50;
    keys("S\x01\x43");
    cok_train_knight(&game);
    CHECK(logged("is too inexperienced;") && c[0x5c] == 3 && c[0x100] == 3);
    CHECK(c[0x11b] == 32 && c[0x62] == (uint8_t)(50 - (8 + 1)));
    /* A Rose knight has nothing to petition for. */
    keys("S");
    cok_train_knight(&game);
    CHECK(!logged("petitioning"));
    /* A knight of no order petitions to become a "Knight Of The ", with
     * only Exit offered; F9 changes it all the same. */
    c[0x5c] = 0;
    keys("SE");
    cok_train_knight(&game);
    CHECK(logged("print: Knight Of The ;") && !logged("inexperienced") && !logged("qualify") &&
          logged("menu: Exit;") && c[0x5c] == 0);
    keys("S\x01\x43");
    cok_train_knight(&game);
    CHECK(game.vm.status == COK_ECL_OK && c[0x5c] == 1);
    empty_party();
}

/* Modify (4def:28fa). */
static void test_modify(void)
{
    empty_party();
    cok_character *f = add(member("BOB", 2, 1));
    uint8_t *c = f->record;
    c[0x5b] = 2;
    put32(c + 0x116, 3000);
    keys("S");
    cok_modify(&game);
    CHECK(logged("BOB can't be modified."));
    /* Past 18, Add raises a fighter's exceptional strength up to the race's
     * most (a man's 100); Keep makes the scores the base. */
    put32(c + 0x116, 2000);
    c[0x10] = c[0x11] = 18;
    c[0x1c] = c[0x1d] = 50;
    keys("SAAAK");
    cok_modify(&game);
    CHECK(c[0x11] == 18 && c[0x1c] == 53 && c[0x1d] == 53 && c[0xe8] == 1);
    /* Exit restores the scores and the name, and sets the hit points to
     * the maximum. */
    c[0x197] = 3;
    keys("SAE");
    cok_modify(&game);
    CHECK(c[0x1c] == 53 && c[0x197] == c[0x62]);
    /* In the name, an upper-case K is typed and then keeps. */
    keys("S\x01P\x01P\x01P\x01P\x01P\x01P" "K");
    cok_modify(&game);
    CHECK(c[1] == 'K' && c[0] == 3 && c[0xe8] == 1);
    /* Keep's hit points at full divide a negative bonus as a byte: a
     * cleric/fighter of constitution 5 and a maximum of 8 gets 137. */
    c[0xf9] = 1;
    c[0xfb] = 1;
    c[0x5b] = 8;
    c[0x18] = c[0x19] = 5;
    c[0x62] = c[0x197] = 8;
    put32(c + 0x116, 2001);
    keys("SK");
    cok_modify(&game);
    CHECK(c[0x11b] == 137);
    /* The least and most hit points. */
    uint8_t r[COK_CHARACTER_SIZE] = {0}, most;
    r[0xfb] = 1;
    r[0x19] = 16;
    CHECK(cok_modify_least(r) == 3 && cok_modify_most(r, r, &most) && most == 12);
    r[0x19] = 2;
    CHECK(!cok_modify_most(r, r, &most));
    empty_party();
}

/* The icon (4def:3c61, 408b). */
static void test_icon(void)
{
    uint8_t c[COK_CHARACTER_SIZE] = {0};
    c[0x5a] = 5;
    cok_create_icon(c);
    CHECK(c[0x135] == 3 && c[0x136] == 5);
    c[0x5a] = 6;
    c[0x109] = 1;
    c[0x138] = 1;
    c[0xfd] = 1;
    cok_create_icon(c);
    CHECK(c[0x135] == 7 && c[0x136] == 1);
    c[0x109] = 0;
    c[0x138] = 2;
    c[0xfe] = 1;
    c[0xfd] = 0;
    cok_create_icon(c);
    CHECK(c[0x135] == 5 && c[0x136] == 0x1d);
    empty_party();
    cok_character *k = add(member("ICON", 2, 1));
    uint8_t *r = k->record;
    r[0x135] = 5;
    r[0x136] = 0x18;
    r[0x138] = 2;
    static const uint8_t colours[6] = {0x91, 0xa2, 0xb3, 0xc4, 0xe6, 0xf7};
    memcpy(r + 0x139, colours, 6);
    /* "Weapon" in Parts edits the body; Keep, then Next on the head and
     * Exit undoes it; then the size, and Yes. */
    keys("PWNKHNEESSKEY");
    CHECK(cok_icon_edit(&game));
    CHECK(r[0x136] == 0x19 && r[0x135] == 5 && r[0x138] == 1);
    CHECK(game.icons[r[0x137]][0].pixels != NULL && game.icons[12][0].pixels == NULL);
    CHECK(logged("menu: Weapon Body Hair Shield Arm Leg Exit") == false);
    /* The colours: 2nd-color edits the high nibble. */
    keys("2SNKEEY");
    CHECK(cok_icon_edit(&game) && r[0x13d] == 0xf6);
    /* A space in Parts leaves the editor with nothing to edit, for good:
     * the keys run out. */
    keys("P NPKEEEEY");
    CHECK(!cok_icon_edit(&game) && game.input_ended);
    empty_party();
}

/* Creation (4def:06dd): a human thief of seed 12345, saved to the roster
 * (the record the original makes, as an emulator run gave it). */
static void test_create(void)
{
    empty_party();
    snprintf(game.save_dir, sizeof game.save_dir, "%s", dir);
    game.vm.seed = 12345;
    game.vm.mem7c00[0x2a8] = 0x1234;
    keys("\x01P\x01P\x01P\x01P\x01P\x01PSS\x01P\x01P\x01PS\x01P\x01PSNBOB\r\x1bYY");
    cok_create(&game);
    CHECK(logged("roster: BOB.WHO;") && game.vm.character == NULL && game.party.count == 0);
    /* The hall's word comes back as its low byte. */
    CHECK(game.vm.mem7c00[0x2a8] == 0x34);
    cok_character *c = calloc(1, sizeof *c);
    CHECK(cok_character_read_file(c, dir, "BOB", "WHO", &game.item_types, game.error,
                                  sizeof game.error));
    const uint8_t *r = c->record;
    static const struct { uint16_t at; uint8_t value; } expect[] = {
        {0x10, 0x11}, {0x12, 0x0e}, {0x14, 0x11}, {0x16, 0x0f}, {0x18, 0x0e}, {0x1a, 0x12},
        {0x59, 0x28}, {0x5a, 6}, {0x5b, 6}, {0x60, 0x16}, {0x62, 0x0a}, {0xdb, 0x2a},
        {0xe1, 0x5d}, {0xe2, 0x07}, {0xf3, 9}, {0xff, 2}, {0x10a, 4}, {0x115, 0xa6},
        {0x116, 0xe3}, {0x117, 0x04}, {0x11a, 4}, {0x11b, 0x0a}, {0x135, 5}, {0x136, 5},
        {0x137, 0x0a}, {0x138, 2}, {0x139, 0x91}, {0x197, 0x0a},
    };
    for (size_t i = 0; i < sizeof expect / sizeof *expect; ++i) CHECK(r[expect[i].at] == expect[i].value);
    cok_character_free(c);
    free(c);
    /* A second BOB: "Overwrite BOB? " No, and a new name, which the
     * character takes. */
    game.vm.seed = 1;
    keys("\x01P\x01P\x01P\x01P\x01P\x01PSS\x01PS\x01P\x01PSNBOB\r\x1bYYNBOBBY\r");
    cok_create(&game);
    CHECK(logged("menu: Overwrite BOB? ;") && logged("roster: BOBBY.WHO;"));
    /* Escape at the first list keeps nothing. */
    keys("\x1b");
    cok_create(&game);
    CHECK(!logged("roster:"));
    /* A knight saves its Plate Mail, Shield and Long Sword, not readied. */
    game.vm.seed = 99;
    keys("\x01P\x01P\x01P\x01P\x01P\x01PSS\x01P\x01P\x01P\x01P\x01PSSNSIRK\r\x1bYY");
    cok_create(&game);
    c = calloc(1, sizeof *c);
    CHECK(cok_character_read_file(c, dir, "SIRK", "WHO", &game.item_types, game.error,
                                  sizeof game.error));
    CHECK(c->item_count == 3 && c->items[0][0x2e] == 0x24 && c->items[1][0x2e] == 0x25 &&
          c->items[2][0x2e] == 0x12 && c->items[0][0x34] == 0);
    CHECK(c->record[0x116] == 0xc4 && c->record[0x117] == 0x09 && c->record[0x5c] == 1);
    cok_character_free(c);
    free(c);
    game.vm.mem7c00[0x2a8] = 0;
}

/* The roster: Add's rules and messages, Drop and Remove. */
static void test_roster(void)
{
    empty_party();
    /* The roster holds BOB, BOBBY and SIRK (test_create). An evil
     * character, a paladin, three rangers. */
    cok_character *evil = add(member("EVIL", 2, 1));
    evil->record[0x10a] = 2;
    keys("A");
    cok_roster_add(&game);
    CHECK(logged("item: BOB            ;") && logged("party: BOB joins;") && game.party.count == 2);
    /* SIRK is a knight, not a paladin; make BOBBY a paladin: the evil
     * member keeps it out, with the extra pause. */
    cok_character *p = calloc(1, sizeof *p);
    CHECK(cok_character_read_file(p, dir, "BOBBY", "WHO", &game.item_types, game.error,
                                  sizeof game.error));
    p->record[0xfc] = 1;
    char error[300];
    CHECK(cok_character_write_file(p, dir, "BOBBY", "WHO", error, sizeof error));
    cok_character_free(p);
    free(p);
    keys("A");
    cok_roster_add(&game);
    CHECK(logged("print: paladins do not join with evil scum;") && game.party.count == 2);
    CHECK(logged("item: BOBBY          ;") && !logged("party: BOBBY joins"));
    /* A space before any choice tests a count Add never set, which the
     * start menu's FreeMem leaves above 5: Add ends. */
    keys(" A");
    cok_roster_add(&game);
    CHECK(game.vm.status == COK_ECL_OK && s.at == 1 && game.party.count == 2);
    /* Drop: No breathes a sigh of relief; Yes twice dumps or bids farewell
     * and erases the roster's copy. */
    game.vm.character = cok_party_record(&game.party, 1);
    keys("SN");
    cok_roster_drop(&game);
    CHECK(logged("print: BOB breathes a sigh of relief.;") && game.party.count == 2);
    keys("SYY");
    cok_roster_drop(&game);
    CHECK(logged("print: BOB bids you farewell.;") && game.party.count == 1);
    char path[128];
    snprintf(path, sizeof path, "%s/BOB.WHO", dir);
    CHECK(access(path, F_OK) != 0);
    /* Remove: a player character goes back to the roster. */
    keys("S");
    cok_roster_remove(&game);
    snprintf(path, sizeof path, "%s/EVIL.WHO", dir);
    CHECK(game.party.count == 0 && access(path, F_OK) == 0 && game.vm.mem7c00[0x33e] == 0);
    empty_party();
}

/* The start menu (4def:01b4). */
static void test_menu(void)
{
    empty_party();
    game.vm.mode = 4;
    game.vm.mem7c00[0x2a8] = 0;
    /* With no party: Create, Add, Load, Initialize and Exit; Escape does
     * nothing but mark the game unsaved. */
    game.effects.rolls.saved = 1;
    keys("\x1b");
    cok_start_menu(&game);
    CHECK(logged("item: Initialize Mouse/Joystick;") && !logged("item: Begin"));
    CHECK(game.effects.rolls.saved == 0 && game.vm.mode == 0);
    /* Free training, Helm's, only by its letter, which no row has. */
    game.helm = false;
    keys("");
    CHECK(!cok_start_command(&game, 'J') && !game.free_training);
    game.helm = true;
    CHECK(!cok_start_command(&game, 'J') && game.free_training && logged("Free training on"));
    game.helm = false;
    /* With a party: Train and Knight with a hall; Begin restores the mode
     * and closes the hall. */
    add(member("A", 2, 1));
    game.vm.mode = 3;
    game.vm.mem7c00[0x2a8] = 127;
    keys("\x01P\x01P\x01P\x01P\x01P\x01P\x01P\x01P\x01PS");
    cok_start_menu(&game);
    CHECK(logged("item: Train Character;item: Knight Change Classes;"));
    CHECK(logged("choice: Begin Adventuring;") && game.vm.mode == 3);
    CHECK(game.vm.mem7c00[0x2a8] == 0 && !game.free_training);
    /* Exit to DOS: unsaved, No saves and stays; Yes and Yes quit. */
    game.vm.mode = 4;
    game.effects.rolls.saved = 0;
    keys("\x01HSYN\x1b");
    cok_start_menu(&game);
    CHECK(logged("menu: Game NOT saved.  Quit anyway? ;") && logged("menu: A B C D E F G H I J;"));
    CHECK(!game.quit);
    keys("\x01HSYY");
    cok_start_menu(&game);
    CHECK(game.quit);
    /* Pick Character: space does not leave it, Escape does. */
    keys(" \x1b");
    CHECK(!cok_start_pick(&game) && s.at == s.length);
    empty_party();
}

/* The title's menu times out to the demonstration; F10 is D too. */
static void test_title(void)
{
    cok_keyboard k = cok_adventure_keyboard(&game);
    keys("\x03");
    game.timed = true;
    CHECK(cok_menu_timed(&game.screen, &game.font, "Champions of Krynn v1.2", "Play Demo", 13, 15,
                         10, &game.selected, &k, 'D') == 'D');
    keys("\x01\x44");
    CHECK(cok_menu_timed(&game.screen, &game.font, "Champions of Krynn v1.2", "Play Demo", 13, 15,
                         10, &game.selected, &k, 'D') == 'D');
    keys("P");
    CHECK(cok_menu_timed(&game.screen, &game.font, "", "Play Demo", 13, 15, 10, &game.selected,
                         &k, 'D') == 'P');
    game.timed = false;
    /* Without a timed menu the timeout ends the run. */
    keys("\x03");
    CHECK(k.read(k.context) < 0 && game.input_ended);
    /* The title: four waits and the credits; a key while it waits ends it. */
    keys("");
    cok_start_title(&game);
    CHECK(s.waits == 4 && logged("print: Electronic Arts;"));
    keys("\x02x");
    cok_start_title(&game);
    CHECK(s.waits == 0 && s.at == s.length && !logged("Electronic Arts"));
    /* The state a game starts and ends with. */
    game.vm.mem4b00[0x10] = 9;
    cok_start_reset(&game);
    CHECK(game.vm.mem4b00[0x10] == 0 && game.vm.mem4b00[0x1f4] == 3 && game.vm.mem4b00[0xe6] == 1);
    CHECK(game.vm.direction == 2 && game.vm.map_x == 7 && game.vm.map_y == 13 && game.list_top == 1);
    cok_start_startup(&game);
    CHECK(game.vm.direction == 0 && game.vm.mode == 4 && game.speed == 4);
}

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

static cok_character *build(const trainee *t)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    uint8_t *r = c->record;
    r[0] = 1;
    r[1] = 'T';
    for (size_t i = 0; i < 6; ++i) r[0x10 + 2 * i] = r[0x11 + 2 * i] = 12;
    r[0x10] = r[0x11] = t->f.strength;
    r[0x18] = r[0x19] = t->f.constitution;
    r[0x5a] = t->f.race;
    r[0x5b] = t->f.class_;
    r[0x5c] = t->f.knight;
    r[0x5d] = t->f.deity;
    r[0x5e] = t->f.magic;
    uint8_t highest = 0;
    for (size_t i = 0; i < 8; ++i) {
        r[0xf9 + i] = t->levels[i];
        if (t->levels[i] > highest) highest = t->levels[i];
    }
    r[0xd6] = highest;
    put32(r + 0x116, t->f.experience);
    r[0x62] = t->f.max;
    r[0x197] = t->f.hp;
    r[0x11b] = t->f.full;
    r[0x188] = t->f.status;
    r[0x189] = 1;
    r[0x109] = t->f.gender;
    r[0x1c] = r[0x1d] = t->f.exceptional;
    return c;
}

/* Creation, training, the knights' change, Modify and the icon editor
 * against what the original did in the emulator. */
static void test_vectors(void)
{
    snprintf(game.save_dir, sizeof game.save_dir, "%s", dir);
    static char typed[256];
    for (size_t k = 0; k < sizeof created / sizeof *created; ++k) {
        size_t n = 0;
        uint8_t picks[5] = {created[k].race, created[k].gender, created[k].class_,
                            created[k].deity, created[k].align};
        for (size_t p = 0; p < 5; ++p) {
            if (picks[p] == 0) continue;
            for (unsigned d = 1; d < picks[p]; ++d) {
                typed[n++] = 1;
                typed[n++] = 'P';
            }
            typed[n++] = 'S';
        }
        for (unsigned r = 0; r < created[k].rerolls; ++r) typed[n++] = 'Y';
        memcpy(typed + n, "NVEC\r\x1bYY", 9);
        typed[n + 9] = '\0';
        empty_party();
        game.vm.seed = created[k].seed;
        game.vm.mem7c00[0x2a8] = 0;
        keys(typed);
        cok_create(&game);
        CHECK(logged("roster: VEC.WHO;") && game.vm.seed == created[k].after);
        uint32_t h = 0x811c9dc5u;
        static const char *const ext[3] = {"WHO", "STF", "SFX"};
        for (size_t e = 0; e < 3; ++e) {
            char path[128];
            snprintf(path, sizeof path, "%s/VEC.%s", dir, ext[e]);
            FILE *f = fopen(path, "rb");
            if (f == NULL) continue;
            static uint8_t data[4096];
            size_t size = fread(data, 1, sizeof data, f);
            fclose(f);
            remove(path);
            h = fnv(h, data, size);
        }
        CHECK(h == created[k].hash);
    }
    for (size_t k = 0; k < sizeof trained / sizeof *trained; ++k) {
        empty_party();
        cok_character *c = add(build(&trained[k].who));
        game.vm.seed = trained[k].seed;
        game.vm.mem7c00[0x2a8] = trained[k].hall;
        game.free_training = trained[k].free != 0;
        char k3[4] = {'S', trained[k].answer, 'L', '\0'};
        keys(trained[k].creating ? "" : k3);
        if (trained[k].creating)
            cok_train_silently(&game, c);
        else
            cok_train(&game);
        CHECK(game.vm.status == COK_ECL_OK && game.vm.seed == trained[k].after);
        CHECK(fnv(0x811c9dc5u, c->record, COK_CHARACTER_SIZE) == trained[k].hash);
    }
    game.free_training = false;
    for (size_t k = 0; k < sizeof promoted / sizeof *promoted; ++k) {
        empty_party();
        cok_character *c = add(build(&promoted[k].who));
        CHECK(cok_train_promote(&game, c));
        CHECK(fnv(0x811c9dc5u, c->record, COK_CHARACTER_SIZE) == promoted[k].hash);
    }
    for (size_t k = 0; k < sizeof modified / sizeof *modified; ++k) {
        empty_party();
        cok_character *c = add(build(&modified[k].who));
        snprintf(typed, sizeof typed, "S%s", modified[k].keys);
        keys(typed);
        game.selected = 1;
        cok_modify(&game);
        CHECK(game.vm.status == COK_ECL_OK && !game.input_ended);
        CHECK(fnv(0x811c9dc5u, c->record, COK_CHARACTER_SIZE) == modified[k].hash);
    }
    for (size_t k = 0; k < sizeof edited / sizeof *edited; ++k) {
        empty_party();
        cok_character *c = add(calloc(1, sizeof *c));
        uint8_t *r = c->record;
        r[0x135] = edited[k].head;
        r[0x136] = edited[k].body;
        r[0x137] = 1;
        r[0x138] = edited[k].size;
        memcpy(r + 0x139, edited[k].colours, 6);
        game.vm.character = r;
        keys(edited[k].keys);
        game.selected = 1;
        CHECK(cok_icon_edit(&game));
        CHECK(fnv(0x811c9dc5u, r + 0x135, 10) == edited[k].hash);
    }
    empty_party();
}

int main(void)
{
    snprintf(dir, sizeof dir, "build/start_XXXXXX");
    CHECK(mkdtemp(dir) != NULL);
    cok_keyboard k = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .key_pending = pending, .delay = delay,
                                 .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &k, &hooks));
    test_tables();
    test_experience();
    test_hit_points();
    test_train();
    test_knight();
    test_modify();
    test_icon();
    test_create();
    test_roster();
    test_menu();
    test_title();
    test_vectors();
    test_quirks();
    test_roster_rules();
    test_demo();
    test_edges();
    cok_adventure_close(&game);
    char command[128];
    snprintf(command, sizeof command, "rm -r %s", dir);
    CHECK(system(command) == 0);
    puts("start tests passed");
    return 0;
}
