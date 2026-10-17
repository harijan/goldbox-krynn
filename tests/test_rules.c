/* The games' descriptors (src/rules.c): --game's lookup and its error's
 * list, and that every file Champions' descriptor names is in COKAssets/
 * (the font's record too) and, with the original's saved games in SAVE/,
 * that its saved games' and characters' names are theirs. Without
 * COKAssets/ or SAVE/ those checks are skipped. */
#include "dax.h"
#include "rules.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const rules = &cok_rules_champions;

static bool exists(const char *dir, const char *name, const char *extension)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s%s", dir, name, extension);
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* <stem>.DAX. */
static void check_single(const char *stem)
{
    if (!exists("COKAssets", stem, ".DAX")) {
        fprintf(stderr, "COKAssets/%s.DAX is not there\n", stem);
        exit(1);
    }
}

/* <stem><n>.DAX for some n, as the ECL file (or its digit) picks it: a
 * misspelt stem has none. */
static void check_numbered(const char *stem, const char *kind)
{
    unsigned found = 0;
    for (unsigned n = 0; n <= 9; ++n) {
        char name[64];
        snprintf(name, sizeof name, "%s%u%s", stem, n, kind);
        found += exists("COKAssets", name, ".DAX");
    }
    if (found == 0) {
        fprintf(stderr, "COKAssets has no %s<n>%s.DAX\n", stem, kind);
        exit(1);
    }
}

/* The record is the one Champions reads (expected), and its archive is
 * there and holds it. */
static void check_record(const cok_rules_record *r, uint8_t expected)
{
    CHECK(r->record == expected);
    char path[64];
    if (r->file == 0)
        snprintf(path, sizeof path, "COKAssets/%s.DAX", r->stem);
    else
        snprintf(path, sizeof path, "COKAssets/%s%u.DAX", r->stem, r->file);
    dax_archive archive = {0};
    CHECK(dax_open(path, &archive) == DAX_OK);
    bool found = false;
    for (size_t i = 0; i < archive.count; ++i) {
        dax_record record;
        CHECK(dax_record_at(&archive, i, &record) == DAX_OK);
        found |= record.id == r->record;
    }
    dax_close(&archive);
    CHECK(found);
}

static void test_lookup(void)
{
    CHECK(cok_rules_find("champions") == &cok_rules_champions);
    CHECK(strcmp(cok_rules_champions.id, "champions") == 0);
    /* An unknown name: none, and the error lists the names known. */
    CHECK(cok_rules_find("death-knights") == NULL);
    CHECK(cok_rules_find("Champions") == NULL);
    CHECK(cok_rules_find("") == NULL);
    CHECK(cok_rules_find("champion") == NULL);
    CHECK(strcmp(cok_rules_names(), "champions") == 0);
    CHECK(strcmp(cok_rules_names(), "champions") == 0); /* the same the second time */
    /* The default assets, and the names shown. */
    CHECK(strcmp(rules->assets, "COKAssets") == 0);
    CHECK(strcmp(rules->text.name, "Champions of Krynn") == 0);
    CHECK(strcmp(rules->text.version, "Champions of Krynn v1.2") == 0);
    CHECK(strcmp(rules->text.version_again, "Champions Of Krynn v1.2") == 0);
    /* The digit of a file's number, as Str into a string[1] makes it. */
    char (*digit)(unsigned) = rules->archives.file_digit;
    CHECK(digit(1) == '1');
    CHECK(digit(3) == '3');
    CHECK(digit(0) == '0');
    CHECK(digit(23) == '2');
    CHECK(digit(255) == '2');
}

static void test_assets(void)
{
    const cok_rules_archives *a = &rules->archives;
    const char *const numbered[] = {a->ecl,  a->geo,  a->walldef, a->tiles, a->pic,     a->sprite,
                                    a->big,  a->head, a->body,    a->icons, a->treasure};
    for (size_t i = 0; i < sizeof numbered / sizeof *numbered; ++i) check_numbered(numbered[i], "");
    check_numbered(a->monster, a->monster_record);
    check_numbered(a->monster, a->monster_effects);
    check_numbered(a->monster, a->monster_items);
    const char *const singles[] = {a->missiles,      a->icon_head, a->icon_body,
                                   a->sky,           a->title,     a->dungeon,
                                   a->wilderness,    a->random,    a->music_drivers,
                                   a->music_score};
    for (size_t i = 0; i < sizeof singles / sizeof *singles; ++i) check_single(singles[i]);
    CHECK(exists("COKAssets", rules->files.config, ""));
    CHECK(exists("COKAssets", rules->files.item_types, ""));
    /* The archives the image tools export. */
    const char *const *lists[] = {rules->images.delta, rules->images.collection,
                                  rules->images.single};
    size_t listed = 0;
    for (size_t i = 0; i < 3; ++i)
        for (const char *const *stem = lists[i]; *stem != NULL; ++stem, ++listed)
            check_single(*stem);
    CHECK(listed == 26); /* the 26 graphics archives tests/test_images_cli.py exports */
    /* The font and the cursor: each record in its file. */
    CHECK(strcmp(a->font.stem, a->tiles) == 0);
    check_record(&a->font, 201);
    check_record(&a->cursor, 1);
}

/* The original's saved games: SAVGAMA.DAT with its characters
 * CHRDATA1-6, the first with no effects. */
static void test_saves(void)
{
    const cok_rules_saves *s = &rules->saves;
    char name[64];
    snprintf(name, sizeof name, "%sA.%s", s->game, s->game_extension);
    CHECK(exists("SAVE", name, ""));
    snprintf(name, sizeof name, "%sA2.", s->character);
    CHECK(exists("SAVE", name, s->record));
    CHECK(exists("SAVE", name, s->items));
    CHECK(exists("SAVE", name, s->effects));
    CHECK(strcmp(s->roster, "WHO") == 0); /* none in SAVE/: the start menu's (4def:37a2) */
    CHECK(strcmp(s->debug, "DEBUG.TXT") == 0);
}

int main(void)
{
    test_lookup();
    struct stat st;
    if (stat("COKAssets", &st) == 0)
        test_assets();
    else
        puts("rules: COKAssets not found; its files' checks skipped");
    if (stat("SAVE/SAVGAMA.DAT", &st) == 0)
        test_saves();
    else
        puts("rules: SAVE/SAVGAMA.DAT not found; the saved games' checks skipped");
    puts("rules: ok");
    return 0;
}
