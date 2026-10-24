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
static void name_of(const cok_character_layout *L, const uint8_t *c, char out[16])
{
    uint8_t stored = cok_field_u8_at(L, c, COK_FIELD_NAME, 0);
    size_t length = stored > 15 ? 15 : stored;
    memcpy(out, c + cok_field_offset_at(L, COK_FIELD_NAME, 1), length);
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

/* Whether file is a roster character's, base.WHO in any case (the rules'
 * saves.roster, upper case). */
static bool is_who(const char *file, const char *who, size_t *base_length)
{
    size_t length = strlen(file), n = strlen(who);
    if (length < n + 2 || file[length - n - 1] != '.') return false;
    const char *ext = file + length - n;
    for (size_t i = 0; i < n; ++i) {
        char c = ext[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != who[i]) return false;
    }
    *base_length = length - n - 1;
    return true;
}

/* The roster in dir of the game of rules (the game's own, or the one it
 * imports from), by its roster extension and record size, leaving out a
 * record whose byte flag (field, index) is 0x80 or more and one whose name
 * a member of the party has. */
static size_t list_roster(cok_adventure *game, const char *dir, const cok_rules *rules,
                          cok_field flag, uint8_t flag_index, cok_roster_entry *entries,
                          size_t max)
{
    if (dir[0] == '\0') return 0;
    DIR *d = opendir(dir);
    if (d == NULL) return 0;
    size_t count = 0;
    struct dirent *entry;
    const cok_character_layout *L = rules->character, *party = cok_game_rules(game)->character;
    while ((entry = readdir(d)) != NULL && count < max) {
        size_t base_length;
        if (!is_who(entry->d_name, rules->saves.roster, &base_length) ||
            base_length >= sizeof entries->base)
            continue;
        char path[sizeof game->save_dir + 300];
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        FILE *f = fopen(path, "rb");
        if (f == NULL) continue;
        uint8_t record[COK_CHARACTER_MAX + 1];
        size_t size = fread(record, 1, sizeof record, f);
        fclose(f);
        if (size != L->file_size || record[cok_field_offset_at(L, flag, flag_index)] > 0x7f)
            continue;
        /* BlockRead of 16 bytes into a zeroed node: past 15 characters it
         * holds zeros. */
        char name[16], shown[16];
        name_of(L, record, name);
        padded(name, shown);
        bool joined = false;
        for (size_t i = 0; i < game->party.count && !joined; ++i) {
            char member[16], member_shown[16];
            name_of(party, game->party.members[i]->record, member);
            padded(member, member_shown);
            joined = strcmp(member_shown, shown) == 0;
        }
        if (joined) continue;
        cok_roster_entry *e = &entries[count++];
        memcpy(e->base, entry->d_name, base_length);
        e->base[base_length] = '\0';
        snprintf(e->shown, sizeof e->shown, "%s", shown);
    }
    closedir(d);
    qsort(entries, count, sizeof *entries, by_base);
    return count;
}

size_t cok_roster_list(cok_adventure *game, cok_roster_entry *entries, size_t max)
{
    const cok_rules *rules = cok_game_rules(game);
    return list_roster(game, game->save_dir, rules, (cok_field)rules->saves.roster_flag,
                       rules->saves.roster_flag_index, entries, max);
}

void cok_roster_erase(cok_adventure *game, const uint8_t *record)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (game->save_dir[0] == '\0') return;
    char name[16], base[9], path[sizeof game->save_dir + 32];
    name_of(L, record, name);
    cok_party_file_name(name, base);
    const cok_rules_saves *s = &cok_game_rules(game)->saves;
    /* dkk 46b2:09dc: its spells' block's file too (DS:57ac, 71c0-71ca). */
    const char *const extensions[4] = {s->roster, s->items, s->effects, s->spells};
    for (size_t i = 0; i < 4 && extensions[i] != NULL; ++i) {
        snprintf(path, sizeof path, "%s/%s.%s", game->save_dir, base, extensions[i]);
        remove(path); /* 169c:0154: only if it is there */
    }
}

bool cok_roster_save(cok_adventure *game, cok_character *character)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (game->save_dir[0] == '\0') {
        cok_adventure_log(game, "error", "no directory to save characters in");
        return false;
    }
    /* dkk 46b2:0ad6: the game's own files from now on (DS:c468). */
    if (cok_game_rules(game)->characters.import != NULL) {
        game->own_files = true;
        game->own_files_set = true;
    }
    uint8_t *c = character->record;
    char name[16], base[41], path[sizeof game->save_dir + 64];
    name_of(L, c, name);
    char file[9];
    cok_party_roster_name(name, file);
    snprintf(base, sizeof base, "%s", file);
    for (;;) {
        snprintf(path, sizeof path, "%s/%s.%s", game->save_dir, base,
                 cok_game_rules(game)->saves.roster);
        if (!exists(path)) break;
        char prompt[64];
        snprintf(prompt, sizeof prompt, cok_game_rules(game)->start.overwrite, base);
        int answer = cok_camp_yes_no(game, prompt, cok_game_rules(game)->start.ask_colour);
        if (answer < 0) return false;
        if (answer == 'Y') break;
        cok_keyboard keys = cok_adventure_keyboard_raw(game);
        base[0] = '\0';
        while (base[0] == '\0') {
            const char *ask = cok_game_rules(game)->start.new_name;
            cok_adventure_log(game, "input", ask);
            if (!cok_text_input(&game->screen, &game->font, ask, 10, 0, 8, &keys, base))
                return false;
        }
        cok_adventure_log(game, "input", base);
        /* The character takes the file's name. */
        size_t length = strlen(base) > 15 ? 15 : strlen(base);
        cok_field_set8_at(L, c, COK_FIELD_NAME, 0, (uint8_t)length);
        memcpy(c + cok_field_offset_at(L, COK_FIELD_NAME, 1), base, length);
    }
    char error[300];
    if (strpbrk(base, "/\\") != NULL) {
        /* A file name DOS would take as a path; the port writes nothing. */
        snprintf(error, sizeof error, "%s: not a file name", base);
        cok_adventure_log(game, "error", error);
        return false;
    }
    if (!cok_character_write_file(character, cok_game_rules(game), game->save_dir, base,
                                  cok_game_rules(game)->saves.roster, error, sizeof error)) {
        cok_adventure_log(game, "error", error);
        return false;
    }
    char text[64];
    snprintf(text, sizeof text, "%s.%s", base, cok_game_rules(game)->saves.roster);
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
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (!cok_start_pick(game)) return;
    size_t index;
    if (!selected_index(game, &index)) {
        cok_adventure_party(game);
        return;
    }
    uint8_t *c = game->vm.character;
    const cok_rules_start *start = &cok_game_rules(game)->start;
    char name[16], text[80];
    name_of(L, c, name);
    snprintf(text, sizeof text, start->drop, name);
    int answer = cok_camp_yes_no(game, text, start->ask_colour);
    if (answer == 'Y') answer = cok_camp_yes_no(game, start->drop_sure, start->ask_colour);
    if (answer < 0) return;
    if (answer == 'Y') {
        if (cok_field_u8(L, c, COK_FIELD_ACTIVE) == 0)
            snprintf(text, sizeof text, start->dumped, name);
        else
            snprintf(text, sizeof text, start->farewell, name);
        cok_adventure_log(game, "print", text);
        cok_camp_notice(game, text);
        cok_roster_erase(game, c);
        if (!cok_treasure_remove_record(game, index, false, true, false)) return;
    } else {
        snprintf(text, sizeof text, start->relief, name);
        cok_adventure_log(game, "print", text);
        cok_camp_notice(game, text);
    }
    cok_adventure_party(game);
}

void cok_roster_remove(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    if (game->vm.character == NULL || !cok_start_pick(game)) return;
    size_t index;
    if (!selected_index(game, &index)) {
        undefined(game, "Remove with no character selected reads through NULL (4def:0446)");
        return;
    }
    cok_character *c = game->party.members[index];
    if (cok_field_u8(L, c->record, COK_FIELD_NPC) >= 0x80) {
        cok_roster_drop(game);
        return;
    }
    /* The character leaves the party whether it was saved or not. */
    cok_roster_save(game, c);
    if (game->vm.abort) return;
    cok_treasure_remove_record(game, index, false, true, false);
}

/* Whether record's name is the same string as other's, and its +0x115. */
static bool same(const cok_character_layout *L, const uint8_t *a, const uint8_t *b)
{
    uint8_t length = cok_field_u8_at(L, a, COK_FIELD_NAME, 0);
    size_t text = cok_field_offset_at(L, COK_FIELD_NAME, 1);
    return length == cok_field_u8_at(L, b, COK_FIELD_NAME, 0) &&
           memcmp(a + text, b + text, length > 15 ? 15 : length) == 0 &&
           cok_field_u8(L, a, COK_FIELD_MONSTER_ID) == cok_field_u8(L, b, COK_FIELD_MONSTER_ID);
}

/* "from where" (dkk 46b2:1ef0, 49cb:343c: 6a69:0f1b over DS:3337): the
 * game's own (false in *other), the game it imports from (true), or
 * nothing for Exit or Escape (false returned). A game that imports none
 * asks nothing: its own. A game whose menus go by index reads the three
 * by index (6a69:00cb with 2, no keypad); another special key goes on
 * with what the original leaves unset, which stops the run citing unset,
 * or for NULL (Add) with the last choice of whose files (DS:c468), which
 * stops only if none was made yet. */
static bool from_where(cok_adventure *game, const char *prompt, const char *unset, bool *other)
{
    const cok_rules_start *start = &cok_game_rules(game)->start;
    *other = false;
    if (cok_game_rules(game)->characters.import == NULL) return true;
    /* 110f:0584 / 14e4:0b6b: the window cleared first. */
    cok_picture_fill(&game->screen, 1, 8, 0x26, 0x16 * 8, 0);
    bool special;
    if (cok_game_rules(game)->menus.routine == COK_MENUS_INDEXED) {
        cok_menu_items items;
        cok_menu_items_upto(&items, 2);
        int key = cok_camp_pick_menu(game, prompt, start->from_items, &items, false, false,
                                     &special);
        if (key < 0) return false;
        if (key == 0 || key == 1) {
            char word[2] = {key == 0 ? start->from_items[0] : strchr(start->from_items, ' ')[1],
                            '\0'};
            cok_adventure_log(game, "choice", word);
            *other = key == 1;
            game->own_files = !*other; /* DS:c468 (46b2:1f31, 1f78; 49cb:3490, 349b) */
            game->own_files_set = true;
            return true;
        }
        if (key == 2 || (special && key == 0x1b)) return false;
        if (unset == NULL && game->own_files_set) {
            /* Add (49cb:34a9) goes on with DS:c468 as it stood. */
            *other = !game->own_files;
            return true;
        }
        undefined(game, unset != NULL ? unset
                                      : "Add from where with a special key reads DS:c468, never "
                                        "set since startup (49cb:34a9; past the image EXEPACK "
                                        "unpacks, cleared by no startup code)");
        return false;
    }
    int key = cok_camp_menu(game, prompt, start->from_items, false, false, &special);
    if (key <= 0) return false;
    char letter[2] = {(char)key, '\0'};
    cok_adventure_log(game, "choice", letter);
    if (key != start->from_items[0] && key != strchr(start->from_items, ' ')[1]) return false;
    *other = key != start->from_items[0];
    return true;
}

void cok_roster_add(cok_adventure *game)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_rules_start *start = &rules->start;
    const cok_character_layout *L = rules->character;
    cok_ecl *vm = &game->vm;
    bool other;
    if (!from_where(game, start->add_from, NULL, &other)) return;
    /* The roster read: the game's own, or the other game's (dkk
     * 46b2:0342 with DS:c468 0: *.WHO in DS:99a4 by its own NPC flag),
     * whose characters are imported. */
    const cok_rules *source = other ? rules->characters.import->from : rules;
    const char *dir = other ? game->import_dir : game->save_dir;
    static cok_roster_entry entries[256];
    size_t count = other ? list_roster(game, dir, source, COK_FIELD_NPC, 0, entries, 256)
                         : cok_roster_list(game, entries, 256);
    if (count == 0) return;
    char text[256][41];
    cok_menu_row rows[256];
    cok_adventure_log(game, "list", start->add_prompt);
    for (size_t i = 0; i < count; ++i) {
        snprintf(text[i], sizeof text[i], "%s", entries[i].shown);
        rows[i] = (cok_menu_row){text[i], false};
        cok_adventure_log(game, "item", text[i]);
    }
    /* Exit is offered by a local 4def:37a2 never sets: what FreeMem's
     * return address left there, 0x2e (dkk 49cb:343c passes 1). */
    cok_menu_style style = {start->add_prompt, start->add_select, 15, 10, 13, true};
    bool redraw = true, evil = false, paladin = false, tried = false;
    char paladin_name[16] = "";
    int index = 0;
    uint8_t players = 0;
    cok_keyboard keys = cok_adventure_keyboard(game);
    int key;
    bool indexed = rules->menus.routine == COK_MENUS_INDEXED;
    do {
        if (indexed) {
            /* dkk 49cb:34db-3526: 6a69:00cb(0), then 1b82 over the roster with
             * Add (DS:3356) and Exit, the last 0; Exit and Escape (0x1b)
             * leave (1), a row picked (0) is tried. 0f1b gives this menu
             * nothing else, so the loop's count of players ([bp-0x1e],
             * set only once a row is tried) is never read unset. */
            cok_menu_items items;
            cok_menu_items_upto(&items, 0);
            bool special = false, picked;
            key = cok_menu_indexed_rows(&game->screen, &game->font, rows, count,
                                        (cok_text_window){1, 2, 0x26, 0x16}, &style,
                                        start->add_select, &items, 0, &redraw, &index,
                                        &game->list_top, &game->selected, &keys, &special,
                                        &picked);
            if (key < 0) return;
            game->menu_special = special; /* DS:c0f8 */
            if (special && key == 0x1b) key = 'E';
            else if (!special && key == 0 && picked) key = 'A';
            else key = ' ';
        } else {
            key = cok_menu_rows(&game->screen, &game->font, rows, count,
                                (cok_text_window){1, 2, 0x26, 0x16}, &style, &redraw, &index,
                                &game->list_top, &game->selected, &keys);
        }
        if (key < 0) return;
        /* A space first reaches the loop's test of a count not yet set
         * (4def:3ae7): FreeMem's leftover from freeing the start menu's last
         * row (4def:0381), the offset of the free block below it (8) or,
         * when the two merge, DS's low byte (0xc6). Every play history
         * tried leaves one of these, above 5, so the space leaves Add; a
         * later Add after one that leaked its file list may find 0 and
         * ignore it. */
        if (key == ' ' && !tried && start->add_space_leaves) return;
        if ((key != 0x0d && key != 'A') || text[index][0] == '*') continue;
        /* 67b5:0c7b, then 4b6d:11e5's message (4b6d:1283); dkk 6a69:145d
         * clears row 24 alone. */
        if (start->add_loading)
            cok_adventure_loading(game);
        else
            cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
        cok_character *c = malloc(sizeof *c);
        if (c == NULL) {
            undefined(game, "out of memory");
            return;
        }
        bool read = other ? cok_character_import(c, rules, dir, entries[index].base,
                                                 source->saves.roster, &game->item_types,
                                                 game->error, sizeof game->error)
                          : cok_character_read_file(c, rules, dir, entries[index].base,
                                                    rules->saves.roster, &game->item_types,
                                                    game->error, sizeof game->error);
        if (!read) {
            cok_character_free(c);
            free(c);
            cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
            return;
        }
        cok_adventure_flush(game); /* 4b6d:15ce, in 11e5 before the recompute */
        uint8_t *r = c->record;
        char marked[41];
        snprintf(marked, sizeof marked, "* %s", text[index]);
        snprintf(text[index], sizeof text[index], "%s", marked);
        tried = true;
        players = 0;
        bool accept, first = game->party.count == 0;
        if (first) {
            cok_ecl_set_var(vm, COK_VAR_PARTY_SIZE, 0);
            accept = true;
        } else {
            uint8_t rangers = 0;
            bool duplicate = false;
            for (size_t i = 0; i < game->party.count && !duplicate; ++i) {
                const uint8_t *p = game->party.members[i]->record;
                if (same(L, p, r)) {
                    duplicate = true;
                    break;
                }
                if (cok_field_u8(L, p, COK_FIELD_NPC) < 0x80) ++players;
                if (cok_field_s8_at(L, p, COK_FIELD_CLASS_LEVEL, 4) > 0) ++rangers;
                if (!start->paladins) continue;
                if ((cok_field_s8(L, p, COK_FIELD_ALIGNMENT) + 1) % 3 == 0) evil = true;
                if (cok_field_s8_at(L, p, COK_FIELD_CLASS_LEVEL, 3) > 0) {
                    paladin = true;
                    name_of(L, p, paladin_name);
                }
            }
            accept = !duplicate &&
                     (cok_field_u8(L, r, COK_FIELD_NPC) < 0x80
                          ? players < 6
                          : cok_ecl_var(vm, COK_VAR_PARTY_SIZE) < 8) &&
                     (cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 4) == 0 || rangers < 3) &&
                     (!start->paladins ||
                      ((cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, 3) == 0 || !evil) &&
                       ((cok_field_s8(L, r, COK_FIELD_ALIGNMENT) + 1) % 3 != 0 || !paladin)));
            if (!accept) {
                if (start->add_unmarks) snprintf(text[index], sizeof text[index], "%s", marked + 2);
                char message[64] = "";
                bool extra = false;
                if (start->paladins && cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 3) > 0 &&
                    evil) {
                    snprintf(message, sizeof message, "paladins do not join with evil scum");
                    extra = true;
                } else if (cok_field_s8_at(L, r, COK_FIELD_CLASS_LEVEL, 4) > 0 && rangers > 2) {
                    snprintf(message, sizeof message, "%s", start->rangers);
                } else if (start->paladins &&
                           (cok_field_s8(L, r, COK_FIELD_ALIGNMENT) + 1) % 3 == 0 && paladin) {
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
            if (!cok_party_add(&game->party, rules, c)) {
                cok_character_free(c);
                free(c);
                undefined(game, "a 73rd record in the list");
                return;
            }
            char error[300];
            if (cok_field_u8(L, r, COK_FIELD_NPC) >= 0x80 &&
                !cok_character_levels(c, rules, &game->item_types, error, sizeof error)) {
                undefined(game, error);
                return;
            }
            cok_ecl_add_var(vm, COK_VAR_PARTY_SIZE, 1);
            vm->character = r;
            if (!cok_arena_party_icon(game, c, true)) return;
            /* The first to join is not counted (4def:38c3). */
            if (!first && cok_field_u8(L, r, COK_FIELD_NPC) < 0x80) ++players;
            char joined[32];
            name_of(L, r, joined);
            snprintf(marked, sizeof marked, "%s joins", joined);
            cok_adventure_log(game, "party", marked);
        }
    } while (key != 'E' && key != 0 && players <= 5 && cok_ecl_var(vm, COK_VAR_PARTY_SIZE) <= 7 &&
             !vm->abort);
}

bool cok_roster_load(cok_adventure *game)
{
    if (game->save_dir[0] == '\0') return false;
    const cok_rules *rules = cok_game_rules(game);
    bool other;
    if (!from_where(game, rules->start.load_from,
                    "Load from where with a special key reads a path never set "
                    "(46b2:1ef0's [bp-0x326])",
                    &other))
        return false;
    /* The other game's saved games (dkk DS:c468 0: SAVGAM?.DAT in
     * DS:99a4), whose party is imported. */
    const cok_rules *source = other ? rules->characters.import->from : rules;
    const char *dir = other ? game->import_dir : game->save_dir;
    if (dir[0] == '\0') return false;
    const cok_rules_saves *s = &source->saves;
    char items[24] = "", path[sizeof game->save_dir + 32];
    cok_menu_items indexes = {0}; /* dkk DS:c120 */
    for (char letter = 'A'; letter <= 'J'; ++letter) {
        snprintf(path, sizeof path, "%s/%s%c.%s", dir, s->game, letter, s->game_extension);
        if (!exists(path)) continue;
        size_t length = strlen(items);
        if (length + 2 < sizeof items) snprintf(items + length, sizeof items - length, "%c ", letter);
        cok_menu_items_add(&indexes, (uint8_t)(letter - 'A'));
    }
    if (items[0] == '\0') return false;
    items[strlen(items) - 1] = '\0';
    bool indexed = rules->menus.routine == COK_MENUS_INDEXED;
    int key;
    for (;;) {
        bool special;
        if (indexed) {
            /* dkk 46b2:207a: those that exist of the ten by index (DS:33e0),
             * no keypad; Escape leaves, an index is its letter (a special
             * key's scan code alike), asked again unless A-J and there. */
            key = cok_camp_pick_menu(game, rules->start.load_prompt, "A B C D E F G H I J",
                                     &indexes, false, false, &special);
            if (key < 0 || (special && key == 0x1b)) return false;
            key = (uint8_t)(key + 'A');
        } else {
            /* The special flag is not tested: scan codes 0x41-0x4a pick
             * letters too. */
            key = cok_camp_menu(game, rules->start.load_prompt, items, false, false, &special);
            if (key <= 0) return false;
        }
        if (key < 'A' || key > 'J') continue;
        snprintf(path, sizeof path, "%s/%s%c.%s", dir, s->game, key, s->game_extension);
        if (exists(path)) break;
    }
    char letter[2] = {(char)key, '\0'};
    cok_adventure_log(game, "choice", letter);
    cok_adventure_loading(game); /* 4b6d:1d06, 1d1e */
    if (!(other ? cok_adventure_import_game(game, path) : cok_adventure_restore(game, path))) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    for (size_t i = 0; i < game->party.count; ++i)
        cok_roster_erase(game, game->party.members[i]->record);
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0);
    return true;
}
