#define _POSIX_C_SOURCE 200809L /* opendir */

#include "roster.h"

#include "arena.h"
#include "camp.h"
#include "start.h"
#include "treasure.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

static bool exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    fclose(f);
    return true;
}

/* The record's name, a string[15]. */
static void name_of(const uint8_t *c, char out[16])
{
    size_t length = c[0] > 15 ? 15 : c[0];
    memcpy(out, c + 1, length);
    out[length] = '\0';
}

/* The name padded with spaces to 15 columns, as 4b6d:008b lists it and
 * compares it. */
static void padded(const char *name, char out[16])
{
    snprintf(out, 16, "%-15.15s", name);
}

static int by_base(const void *a, const void *b)
{
    return strcmp(((const cok_roster_entry *)a)->base, ((const cok_roster_entry *)b)->base);
}

static bool is_who(const char *file, size_t *base_length)
{
    size_t length = strlen(file);
    if (length < 5 || file[length - 4] != '.') return false;
    const char *ext = file + length - 3;
    for (size_t i = 0; i < 3; ++i) {
        char c = ext[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != "WHO"[i]) return false;
    }
    *base_length = length - 4;
    return true;
}

size_t cok_roster_list(cok_adventure *game, cok_roster_entry *entries, size_t max)
{
    if (game->save_dir[0] == '\0') return 0;
    DIR *dir = opendir(game->save_dir);
    if (dir == NULL) return 0;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max) {
        size_t base_length;
        if (!is_who(entry->d_name, &base_length) || base_length >= sizeof entries->base) continue;
        char path[sizeof game->save_dir + 300];
        snprintf(path, sizeof path, "%s/%s", game->save_dir, entry->d_name);
        FILE *f = fopen(path, "rb");
        if (f == NULL) continue;
        uint8_t record[COK_CHARACTER_SIZE + 1];
        size_t size = fread(record, 1, sizeof record, f);
        fclose(f);
        if (size != COK_CHARACTER_SIZE || record[0xf7] > 0x7f) continue;
        /* BlockRead of 16 bytes into a zeroed node: past 15 characters it
         * holds zeros. */
        char name[16], shown[16];
        name_of(record, name);
        padded(name, shown);
        bool joined = false;
        for (size_t i = 0; i < game->party.count && !joined; ++i) {
            char member[16], member_shown[16];
            name_of(game->party.members[i]->record, member);
            padded(member, member_shown);
            joined = strcmp(member_shown, shown) == 0;
        }
        if (joined) continue;
        cok_roster_entry *e = &entries[count++];
        memcpy(e->base, entry->d_name, base_length);
        e->base[base_length] = '\0';
        snprintf(e->shown, sizeof e->shown, "%s", shown);
    }
    closedir(dir);
    qsort(entries, count, sizeof *entries, by_base);
    return count;
}

void cok_roster_erase(cok_adventure *game, const uint8_t *record)
{
    if (game->save_dir[0] == '\0') return;
    char name[16], base[9], path[sizeof game->save_dir + 32];
    name_of(record, name);
    cok_party_file_name(name, base);
    static const char *const extensions[3] = {"WHO", "STF", "SFX"};
    for (size_t i = 0; i < 3; ++i) {
        snprintf(path, sizeof path, "%s/%s.%s", game->save_dir, base, extensions[i]);
        remove(path); /* 169c:0154: only if it is there */
    }
}

bool cok_roster_save(cok_adventure *game, cok_character *character)
{
    if (game->save_dir[0] == '\0') {
        cok_adventure_log(game, "error", "no directory to save characters in");
        return false;
    }
    uint8_t *c = character->record;
    char name[16], base[41], path[sizeof game->save_dir + 64];
    name_of(c, name);
    char file[9];
    cok_party_roster_name(name, file);
    snprintf(base, sizeof base, "%s", file);
    for (;;) {
        snprintf(path, sizeof path, "%s/%s.WHO", game->save_dir, base);
        if (!exists(path)) break;
        char prompt[64];
        snprintf(prompt, sizeof prompt, "Overwrite %s? ", base);
        int answer = cok_camp_yes_no(game, prompt, 14);
        if (answer < 0) return false;
        if (answer == 'Y') break;
        cok_keyboard keys = cok_adventure_keyboard(game);
        base[0] = '\0';
        while (base[0] == '\0') {
            cok_adventure_log(game, "input", "New file name: ");
            if (!cok_text_input(&game->screen, &game->font, "New file name: ", 10, 0, 8, &keys,
                                base))
                return false;
        }
        cok_adventure_log(game, "input", base);
        /* The character takes the file's name. */
        size_t length = strlen(base) > 15 ? 15 : strlen(base);
        c[0] = (uint8_t)length;
        memcpy(c + 1, base, length);
    }
    char error[300];
    if (strpbrk(base, "/\\") != NULL) {
        /* A file name DOS would take as a path; the port writes nothing. */
        snprintf(error, sizeof error, "%s: not a file name", base);
        cok_adventure_log(game, "error", error);
        return false;
    }
    if (!cok_character_write_file(character, game->save_dir, base, "WHO", error, sizeof error)) {
        cok_adventure_log(game, "error", error);
        return false;
    }
    char text[64];
    snprintf(text, sizeof text, "%s.WHO", base);
    cok_adventure_log(game, "roster", text);
    return true;
}

/* The selected record's position, or false if none is selected. */
static bool selected_index(cok_adventure *game, size_t *index)
{
    *index = cok_party_index(&game->party, game->vm.character);
    return *index < game->party.count;
}

void cok_roster_drop(cok_adventure *game)
{
    if (!cok_start_pick(game)) return;
    size_t index;
    if (!selected_index(game, &index)) {
        cok_adventure_party(game);
        return;
    }
    uint8_t *c = game->vm.character;
    char name[16], text[80];
    name_of(c, name);
    snprintf(text, sizeof text, "Drop %s forever? ", name);
    int answer = cok_camp_yes_no(game, text, 14);
    if (answer == 'Y') answer = cok_camp_yes_no(game, "Are you sure? ", 14);
    if (answer < 0) return;
    if (answer == 'Y') {
        if (c[0x189] == 0)
            snprintf(text, sizeof text, "You dump %s out back.", name);
        else
            snprintf(text, sizeof text, "%s bids you farewell.", name);
        cok_adventure_log(game, "print", text);
        cok_camp_notice(game, text);
        cok_roster_erase(game, c);
        if (!cok_treasure_remove_record(game, index, false, true, false)) return;
    } else {
        snprintf(text, sizeof text, "%s breathes a sigh of relief.", name);
        cok_adventure_log(game, "print", text);
        cok_camp_notice(game, text);
    }
    cok_adventure_party(game);
}

void cok_roster_remove(cok_adventure *game)
{
    if (game->vm.character == NULL || !cok_start_pick(game)) return;
    size_t index;
    if (!selected_index(game, &index)) {
        undefined(game, "Remove with no character selected reads through NULL (4def:0446)");
        return;
    }
    cok_character *c = game->party.members[index];
    if (c->record[0xe7] >= 0x80) {
        cok_roster_drop(game);
        return;
    }
    /* The character leaves the party whether it was saved or not. */
    cok_roster_save(game, c);
    if (game->vm.abort) return;
    cok_treasure_remove_record(game, index, false, true, false);
}

/* Whether record's name is the same string as other's, and its +0x115. */
static bool same(const uint8_t *a, const uint8_t *b)
{
    return a[0] == b[0] && memcmp(a + 1, b + 1, a[0] > 15 ? 15 : a[0]) == 0 && a[0x115] == b[0x115];
}

void cok_roster_add(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    static cok_roster_entry entries[256];
    size_t count = cok_roster_list(game, entries, 256);
    if (count == 0) return;
    char text[256][41];
    cok_menu_row rows[256];
    cok_adventure_log(game, "list", "Add a character: ");
    for (size_t i = 0; i < count; ++i) {
        snprintf(text[i], sizeof text[i], "%s", entries[i].shown);
        rows[i] = (cok_menu_row){text[i], false};
        cok_adventure_log(game, "item", text[i]);
    }
    /* Exit is offered by a local 4def:37a2 never sets: what FreeMem's
     * return address left there, 0x2e. */
    cok_menu_style style = {"Add a character: ", "Add ", 15, 10, 13, true};
    bool redraw = true, evil = false, paladin = false, tried = false;
    char paladin_name[16] = "";
    int index = 0;
    uint8_t players = 0;
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key;
    do {
        key = cok_menu_rows(&game->screen, &game->font, rows, count,
                            (cok_text_window){1, 2, 0x26, 0x16}, &style, &redraw, &index,
                            &game->list_top, &game->selected, &keys);
        if (key < 0) return;
        /* A space first reaches the loop's test of a count not yet set
         * (4def:3ae7): FreeMem's leftover from freeing the start menu's last
         * row (4def:0381), the offset of the free block below it (8) or,
         * when the two merge, DS's low byte (0xc6). Every play history
         * tried leaves one of these, above 5, so the space leaves Add; a
         * later Add after one that leaked its file list may find 0 and
         * ignore it. */
        if (key == ' ' && !tried) return;
        if ((key != 0x0d && key != 'A') || text[index][0] == '*') continue;
        cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 67b5:0c7b */
        cok_text_string(&game->screen, &game->font, "Loading...Please Wait", 0, 24, 10, 0);
        cok_character *c = malloc(sizeof *c);
        if (c == NULL) {
            undefined(game, "out of memory");
            return;
        }
        if (!cok_character_read_file(c, game->save_dir, entries[index].base, "WHO",
                                     &game->item_types, game->error, sizeof game->error)) {
            cok_character_free(c);
            free(c);
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return;
        }
        uint8_t *r = c->record;
        char marked[41];
        snprintf(marked, sizeof marked, "* %s", text[index]);
        snprintf(text[index], sizeof text[index], "%s", marked);
        tried = true;
        players = 0;
        bool accept, first = game->party.count == 0;
        if (first) {
            vm->mem7c00[0x33e] = 0;
            accept = true;
        } else {
            uint8_t rangers = 0;
            bool duplicate = false;
            for (size_t i = 0; i < game->party.count && !duplicate; ++i) {
                const uint8_t *p = game->party.members[i]->record;
                if (same(p, r)) {
                    duplicate = true;
                    break;
                }
                if (p[0xe7] < 0x80) ++players;
                if ((int8_t)p[0xfd] > 0) ++rangers;
                if (((int8_t)p[0x10a] + 1) % 3 == 0) evil = true;
                if ((int8_t)p[0xfc] > 0) {
                    paladin = true;
                    name_of(p, paladin_name);
                }
            }
            accept = !duplicate &&
                     (r[0xe7] < 0x80 ? players < 6 : vm->mem7c00[0x33e] < 8) &&
                     (r[0xfc] == 0 || !evil) && (r[0xfd] == 0 || rangers < 3) &&
                     (((int8_t)r[0x10a] + 1) % 3 != 0 || !paladin);
            if (!accept) {
                snprintf(text[index], sizeof text[index], "%s", marked + 2);
                char message[64] = "";
                bool extra = false;
                if ((int8_t)r[0xfc] > 0 && evil) {
                    snprintf(message, sizeof message, "paladins do not join with evil scum");
                    extra = true;
                } else if ((int8_t)r[0xfd] > 0 && rangers > 2) {
                    snprintf(message, sizeof message, "too many rangers in party");
                } else if (((int8_t)r[0x10a] + 1) % 3 == 0 && paladin) {
                    snprintf(message, sizeof message, "%s will tolerate no evil!", paladin_name);
                }
                if (message[0] != '\0') {
                    cok_adventure_log(game, "print", message);
                    cok_camp_notice(game, message);
                    if (extra) cok_adventure_wait(game, game->speed * 100u); /* 1521:0b4b */
                }
                cok_character_free(c);
                free(c);
            }
        }
        if (accept) {
            if (!cok_party_add(&game->party, c)) {
                cok_character_free(c);
                free(c);
                undefined(game, "a 73rd record in the list");
                return;
            }
            char error[300];
            if (r[0xe7] >= 0x80 && !cok_character_levels(c, &game->item_types, error, sizeof error)) {
                undefined(game, error);
                return;
            }
            ++vm->mem7c00[0x33e];
            vm->character = r;
            if (!cok_arena_party_icon(game, c, true)) return;
            /* The first to join is not counted (4def:38c3). */
            if (!first && r[0xe7] < 0x80) ++players;
            char joined[32];
            name_of(r, joined);
            snprintf(marked, sizeof marked, "%s joins", joined);
            cok_adventure_log(game, "party", marked);
        }
    } while (key != 'E' && key != 0 && players <= 5 && vm->mem7c00[0x33e] <= 7 && !vm->abort);
}

bool cok_roster_load(cok_adventure *game)
{
    if (game->save_dir[0] == '\0') return false;
    char items[24] = "", path[sizeof game->save_dir + 32];
    for (char letter = 'A'; letter <= 'J'; ++letter) {
        snprintf(path, sizeof path, "%s/SAVGAM%c.DAT", game->save_dir, letter);
        if (!exists(path)) continue;
        size_t length = strlen(items);
        if (length + 2 < sizeof items) snprintf(items + length, sizeof items - length, "%c ", letter);
    }
    if (items[0] == '\0') return false;
    items[strlen(items) - 1] = '\0';
    int key;
    for (;;) {
        bool special;
        /* The special flag is not tested: scan codes 0x41-0x4a pick
         * letters too. */
        key = cok_camp_menu(game, "Load Which Game: ", items, false, false, &special);
        if (key <= 0) return false;
        if (key < 'A' || key > 'J') continue;
        snprintf(path, sizeof path, "%s/SAVGAM%c.DAT", game->save_dir, key);
        if (exists(path)) break;
    }
    char letter[2] = {(char)key, '\0'};
    cok_adventure_log(game, "choice", letter);
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    cok_text_string(&game->screen, &game->font, "Loading...Please Wait", 0, 24, 10, 0);
    if (!cok_adventure_restore(game, path)) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    for (size_t i = 0; i < game->party.count; ++i)
        cok_roster_erase(game, game->party.members[i]->record);
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    return true;
}
