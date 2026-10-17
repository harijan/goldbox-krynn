#include "rules.h"

#include <stdio.h>
#include <string.h>

/* Champions of Krynn (START.EXE and GAME.OVR, START_FULL.EXE's addresses). */

/* The first digit of the file's number, as Str with a width of 1 makes it
 * (4b6d:161b, 2fd3:1d21, 6961:05b9, 6d21:01d0). */
static char champions_file_digit(unsigned file)
{
    char text[16];
    snprintf(text, sizeof text, "%u", file);
    return text[0];
}

static const char *const champions_deltas[] = {"PIC1", "PIC2", "PIC3", NULL};
static const char *const champions_collections[] = {"SPRIT1", "SPRIT2", "SPRIT3", NULL};
static const char *const champions_singles[] = {
    "TITLE", "BIGPIC1", "BIGPIC2", "BIGPIC3", "CPIC1", "CPIC2", "CPIC3", "CBODY", "CHEAD",
    "BODY2", "BODY3", "HEAD2", "HEAD3", "COMSPR", "CURSOR", "SKY", "TILES", "8X8D1", "8X8D2",
    "8X8D3", NULL};

const cok_rules cok_rules_champions = {
    .id = "champions",
    .assets = "COKAssets",
    .text =
        {
            .name = "Champions of Krynn",
            .version = "Champions of Krynn v1.2",       /* 1000:0134, at startup */
            .version_again = "Champions Of Krynn v1.2", /* 1000:0313's, after the demo */
        },
    .archives =
        {
            .ecl = "ECL",
            .geo = "GEO",
            .walldef = "WALLDEF",
            .tiles = "8X8D",
            .font = {"8X8D", 1, 201}, /* 3e99:005b */
            .pic = "PIC",
            .sprite = "SPRIT",
            .big = "BIGPIC",
            .head = "HEAD",
            .body = "BODY",
            .icons = "CPIC",
            .missiles = "COMSPR",
            .icon_head = "CHEAD",
            .icon_body = "CBODY",
            .sky = "SKY",
            .cursor = {"CURSOR", 0, 1}, /* 3e99:01fd */
            .title = "TITLE",
            .treasure = "ITEM",
            .monster = "MON",
            .monster_record = "CHA",
            .monster_effects = "SPC",
            .monster_items = "ITM",
            .dungeon = "DUNGCOM",
            .wilderness = "WILDCOM",
            .random = "RANDCOM",
            .music_drivers = "MIDI",
            .music_score = "SOUND",
            .file_digit = champions_file_digit,
        },
    .files =
        {
            .config = "KRYNN.CFG",
            .item_types = "ITEMS",
            .install_dir = "C:\\COK\\",
            .save_dir = "SAVE",
        },
    .saves =
        {
            .game = "SAVGAM",
            .game_extension = "DAT",
            .character = "CHRDAT",
            .record = "SAV",
            .roster = "WHO",
            .items = "STF",
            .effects = "SFX",
            .debug = "DEBUG.TXT",
        },
    .images =
        {
            .delta = champions_deltas,
            .collection = champions_collections,
            .single = champions_singles,
        },
};

static const cok_rules *const games[] = {&cok_rules_champions};

const cok_rules *cok_rules_find(const char *id)
{
    for (size_t i = 0; i < sizeof games / sizeof *games; ++i)
        if (strcmp(id, games[i]->id) == 0) return games[i];
    return NULL;
}

const char *cok_rules_names(void)
{
    static char names[256];
    if (names[0] == '\0')
        for (size_t i = 0; i < sizeof games / sizeof *games; ++i) {
            size_t n = strlen(names);
            snprintf(names + n, sizeof names - n, "%s%s", i > 0 ? ", " : "", games[i]->id);
        }
    return names;
}
