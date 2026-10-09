#include "start.h"

#include "camp.h"
#include "create.h"
#include "modify.h"
#include "roster.h"
#include "screen.h"
#include "sheet.h"
#include "train.h"
#include "treasure.h"

#include <stdio.h>
#include <string.h>

/* The start menu's items (DS:0878, a string[40] and a flag each, 0x2a
 * bytes apart). */
static const char *const items[13] = {
    "Create New Character", "Drop Character", "Modify Character", "Train Character",
    "Knight Change Classes", "View Character", "Add Character to Party",
    "Remove Character from Party", "Load Saved Game", "Save Current Game", "Begin Adventuring",
    "Initialize Mouse/Joystick", "Exit to DOS",
};

const char *cok_start_item(unsigned i)
{
    return i < 13 ? items[i] : NULL;
}

static void clear_menu(cok_adventure *game)
{
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 67b5:0c7b */
}

static void moons(const cok_adventure *game, uint16_t out[3])
{
    for (size_t i = 0; i < 3; ++i) out[i] = game->vm.mem4b00[0x1f9 + i];
}

bool cok_start_pick(cok_adventure *game)
{
    int key;
    for (;;) {
        bool special;
        key = cok_camp_menu(game, "Pick Character ", "Select Exit", true, false, &special);
        if (key < 0) return false;
        if (special) {
            cok_camp_pick(game, (uint8_t)key);
            continue;
        }
        if (key == 0 || key == 'E' || key == 'S') break;
    }
    cok_adventure_party(game);
    return key == 'S';
}

/* The state both 3e99:005b and 3e99:0843 leave. */
static void fresh(cok_adventure *game, uint8_t facing)
{
    cok_ecl *vm = &game->vm;
    memset(vm->mem4b00, 0, sizeof vm->mem4b00);
    memset(vm->mem7c00, 0, sizeof vm->mem7c00);
    memset(vm->mem7a00, 0, sizeof vm->mem7a00);
    vm->mem4b00[0x1f4] = 3;  /* difficulty */
    vm->mem4b00[0xe6] = 1;   /* a 3D area */
    for (size_t i = 0; i < 3; ++i) vm->mem4b00[0x1f9 + i] = 2;
    vm->map_x = 7;
    vm->map_y = 13;
    vm->direction = facing;
    vm->ahead = vm->square = 0;
    for (size_t i = 0; i < 3; ++i) {
        game->door_tries[i] = true;
        game->wall_ids[i] = game->wall_slots[i] = -1;
    }
    game->wall_ids[0] = 0;
    game->wall_slots[0] = 1;
    vm->keep_vars = false;
    game->rest_ticks = 0;
    vm->character = NULL;
    vm->file = 1;
    game->speed = 4;
    game->overhead = false;
    game->list_top = 1;
    game->selected = 1;
    vm->mode = 4;
    vm->last_mode = 0;
    game->effects.rolls.saved = 0; /* DS:5885 */
    game->picture_id = COK_ADVENTURE_NO_PICTURE;
    game->big_id = COK_ADVENTURE_NO_PICTURE;
    game->picture_shown = game->sprite_shown = game->big_shown = false;
    game->in_encounter = game->resting = game->moving = false;
    game->party_killed = false;
}

void cok_start_startup(cok_adventure *game)
{
    fresh(game, 0);
    game->demo = false;
}

void cok_start_reset(cok_adventure *game)
{
    fresh(game, 2);
    clear_menu(game);
}

/* 2f55:0000: wait seconds unless a key is pressed, which is thrown away.
 * Returns true for a key. */
static bool title_wait(cok_adventure *game, unsigned seconds)
{
    if (cok_adventure_key_pending(game)) {
        cok_keyboard keys = cok_adventure_keyboard(game);
        keys.read(keys.context); /* 1614:045c */
        return true;
    }
    cok_adventure_wait(game, seconds * 1000u);
    return false;
}

static bool title_picture(cok_adventure *game, uint8_t id, int x, int y)
{
    cok_picture picture = {0};
    if (!cok_adventure_load_image(game, "TITLE", id, -1, &picture)) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    cok_picture_draw(&game->screen, &picture, 0, x, y, 0, NULL); /* 127f:10e7 */
    cok_picture_free(&picture);
    return true;
}

/* The credits (2f55:0267). */
static void credits(cok_adventure *game)
{
    static const struct { uint8_t x, y, fg; const char *text; } lines[30] = {
        {5, 1, 5, "based on an original story by:"}, {15, 2, 14, "Jim Ward"},
        {4, 3, 14, "Victor Penman,"}, {19, 3, 5, "and"}, {24, 3, 14, "Dave Shelley"},
        {2, 5, 5, "game created by:"}, {18, 5, 12, "SSI Special Projects"},
        {2, 7, 12, "Project Director:"}, {20, 7, 12, "Programming"},
        {2, 8, 14, "Victor Penman"}, {20, 8, 14, "Russ Brown"}, {20, 9, 14, "Scot Bayless"},
        {2, 10, 12, "Graphic Arts:"}, {2, 11, 14, "Tom Wahl"},
        {20, 11, 12, "Encounter Design"}, {2, 12, 14, "Fred Butts"},
        {20, 12, 14, "Dave Shelley"}, {2, 13, 14, "Mark Johnson"},
        {20, 13, 14, "Michael Mancuso"}, {2, 14, 14, "Cyrus Lum"},
        {20, 14, 14, "Ken Humphries"}, {2, 15, 14, "Susan Manley"}, {2, 17, 12, "playtest:"},
        {12, 17, 14, "Rick White,Don Mc Clure,"}, {2, 18, 14, "Mike Bench,Cliff Mann, Rick Wilson,"},
        {2, 19, 14, "Erik Flom,James Young,Graeme Bayless"}, {2, 21, 5, "Musical Score:"},
        {20, 21, 5, "Musical Driver:"}, {2, 22, 14, "John Halbleib"},
        {20, 22, 14, "Electronic Arts"},
    };
    cok_screen_credits(&game->screen, &game->view.tiles[4]); /* 1128:0130 */
    for (size_t i = 0; i < 30; ++i) {
        cok_adventure_log(game, "print", lines[i].text);
        cok_text_string(&game->screen, &game->font, lines[i].text, lines[i].x, lines[i].y,
                        lines[i].fg, 0);
    }
}

void cok_start_title(cok_adventure *game)
{
    game->title = true;
    cok_adventure_free_picture(game); /* 6961:0537 */
    cok_adventure_log(game, "unported", "the title's music (1661:000a)");
    bool key = !title_picture(game, 1, 0, 0) || title_wait(game, 5);
    if (!key) key = !title_picture(game, 2, 0, 0) || !title_picture(game, 3, 5, 1) ||
                    title_wait(game, 10);
    if (!key) key = !title_picture(game, 4, 0, 1) || title_wait(game, 10);
    if (!key) {
        cok_picture_fill(&game->screen, 0, 0, 40, 200, 0); /* 1521:0a34 */
        credits(game);
        title_wait(game, 10);
    }
    /* 2f55:006d. */
    cok_picture_fill(&game->screen, 0, 0, 40, 200, 0);
    game->title = false;
}

/* The title's menu (1000:0165, 0335): Play or Demo, which the time running
 * out with no key picks (30 seconds, after a demonstration 10: DS:6e11);
 * so does F10, whose scan code is 0x44, D. */
static int title_menu(cok_adventure *game, const char *prompt)
{
    cok_adventure_log(game, "menu", "Play Demo");
    cok_keyboard keys = cok_adventure_keyboard(game);
    game->timed = game->title = true;
    int key = cok_menu_timed(&game->screen, &game->font, prompt, "Play Demo", 13, 15, 10,
                             &game->selected, &keys, 'D');
    game->timed = false;
    if (key >= 0) {
        game->title = false;
        char text[2] = {(char)key, '\0'};
        cok_adventure_log(game, "choice", key == 'D' ? "Demo" : key == 0 ? "" : text);
    }
    return key;
}

void cok_start_mouse(cok_adventure *game)
{
    static const char *const rows_text[3] = {"mouse OFF", "joystick OFF", "disable both"};
    cok_menu_row rows[3];
    for (size_t i = 0; i < 3; ++i) rows[i] = (cok_menu_row){rows_text[i], false};
    cok_menu_style style = {"Init Mouse/Joystick: ", "Select", 15, 10, 13, true};
    int index = 0;
    cok_keyboard keys = cok_adventure_keyboard(game);
    for (;;) {
        bool redraw = true;
        cok_adventure_log(game, "list", "Init Mouse/Joystick: ");
        int key = cok_menu_rows(&game->screen, &game->font, rows, 3,
                                (cok_text_window){9, 12, 0x26, 0x16}, &style, &redraw, &index,
                                &game->list_top, &game->selected, &keys);
        if (key != 'S') return;
        /* KRYNN.CFG's third line is written N: neither is found. */
        if (index == 0)
            cok_adventure_log(game, "unported", "the mouse driver (1743:0000)");
        else if (index == 1)
            cok_adventure_log(game, "unported", "the joystick (177c:0000)");
    }
}

/* Begin Adventuring (4def:04b2): with a party, the mode back as it was,
 * the frame and party list drawn unless a game was loaded here and the
 * first block will draw them, row 24 cleared and var 0x7ea8 cleared. */
static bool begin(cok_adventure *game, uint8_t mode)
{
    cok_ecl *vm = &game->vm;
    if (game->party.count == 0 && !game->demo) return false;
    vm->mode = mode;
    uint16_t phases[3];
    moons(game, phases);
    if (!vm->keep_vars || game->picture_id == 9) {
        if (vm->mode == 3 && vm->mem4b00[0x138] == 0)
            cok_screen_big(&game->screen, &game->view.tiles[4], phases); /* 1128:0344 */
        else
            cok_screen_adventure(&game->screen, &game->view.tiles[4], phases); /* 1128:0242 */
        cok_adventure_party(game);
    } else if (vm->mem4b00[0xf2] == 0 || vm->mem4b00[0x138] != 0) {
        /* A game loaded here: the adventure frame whatever the mode. */
        cok_screen_adventure(&game->screen, &game->view.tiles[4], phases);
        cok_adventure_party(game);
    }
    clear_menu(game);
    vm->mem7c00[0x2a8] = 0;
    return true;
}

/* Exit to DOS (4def:055a). */
static void leave(cok_adventure *game)
{
    int answer = cok_camp_yes_no(game, "Quit to DOS ", 14);
    if (answer != 'Y') return;
    if (game->party.count > 0 && game->effects.rolls.saved == 0) {
        answer = cok_camp_yes_no(game, "Game NOT saved.  Quit anyway? ", 14);
        if (answer < 0) return;
        if (answer == 'N') {
            cok_camp_save(game);
            return;
        }
    }
    cok_camp_quit(game);
}

/* The items each pass offers (4def:01ef): by whether a character is
 * selected; Train and Knight Change Classes also need a hall (var 0x7ea8)
 * or free training. */
static void flags(cok_adventure *game, bool on[13])
{
    bool any = game->vm.character != NULL;
    bool hall = any && (game->vm.mem7c00[0x2a8] != 0 || game->free_training);
    bool want[13] = {true, any, any, hall, hall, any, true, any, !any, any, any, !any, true};
    memcpy(on, want, sizeof want);
}

/* The mode the start menu was entered with ([bp-0x10]), for Begin. */
static uint8_t entry_mode;

bool cok_start_command(cok_adventure *game, char letter)
{
    bool on[13];
    flags(game, on);
    switch (letter) {
    case 'C': if (on[0]) cok_create(game); break;
    case 'D': if (on[1]) cok_roster_drop(game); break;
    case 'M': if (on[2]) cok_modify(game); break;
    case 'T': if (on[3]) cok_train(game); break;
    case 'K': if (on[4]) cok_train_knight(game); break;
    case 'V':
        if (on[5] && cok_start_pick(game)) {
            bool done;
            cok_sheet(game, COK_SHEET_STALE_UNKNOWN, &done);
        }
        break;
    case 'A': if (on[6]) cok_roster_add(game); break;
    case 'R': if (on[7]) cok_roster_remove(game); break;
    case 'L': if (on[8]) cok_roster_load(game); break;
    case 'S': if (on[9] && game->party.count > 0) cok_camp_save(game); break;
    case 'B': return on[10] && begin(game, entry_mode);
    case 'I': if (on[11]) cok_start_mouse(game); break;
    case 'E': if (on[12]) leave(game); break;
    case 'J':
        /* Helm's free training (4def:05e8), which no item begins with. */
        if (game->helm) {
            game->free_training = !game->free_training;
            cok_camp_notice(game, game->free_training ? "Free training on" : "Free training off");
        }
        break;
    default: break;
    }
    return false;
}

void cok_start_menu(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    uint8_t outer = entry_mode;
    entry_mode = vm->mode;
    vm->mode = 0;
    game->free_training = false;
    int index = 0;
    for (;;) {
        if (vm->abort) break;
        uint16_t phases[3];
        moons(game, phases);
        cok_screen_frame(&game->screen, &game->view.tiles[4], phases, true); /* 1128:0000 */
        if (vm->character != NULL) cok_adventure_party(game);
        bool on[13];
        flags(game, on);
        cok_menu_row rows[13];
        size_t count = 0;
        cok_adventure_log(game, "list", "Choose a FUNCTION ");
        for (size_t i = 0; i < 13; ++i) {
            if (!on[i]) continue;
            rows[count++] = (cok_menu_row){items[i], false};
            cok_adventure_log(game, "item", items[i]);
        }
        cok_menu_style style = {"Choose a FUNCTION ", "Select ", 15, 10, 13, false};
        bool redraw = true;
        cok_keyboard keys = cok_adventure_keyboard(game);
        int key = cok_menu_rows(&game->screen, &game->font, rows, count,
                                (cok_text_window){9, 12, 0x26, 0x16}, &style, &redraw, &index,
                                &game->list_top, &game->selected, &keys);
        if (key < 0) break;
        /* The command is the picked row's first letter; Escape leaves no
         * row, and the original reads 0000:0001, which holds 0. */
        char letter = key == 0 ? '\0' : rows[index].text[0];
        if (key != 0) cok_adventure_log(game, "choice", rows[index].text);
        if (letter != 'E' && letter != 'S') game->effects.rolls.saved = 0; /* DS:5885 */
        if (cok_start_command(game, letter)) break;
    }
    entry_mode = outer;
}

/* 2fd3:3c28 from main: the first block, or in the demonstration ECL2
 * block 0x39's load vector alone; then every member goes (4def:3b0a). */
static cok_ecl_status adventure(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    cok_ecl_status status;
    bool keep = game->demo;
    if (game->demo) {
        /* As 2fd3:3c28 starts, with no party list. */
        game->redraw = true;
        game->files_loaded = game->pieces_loaded = game->map_loaded = false;
        game->frame_pending = false;
        vm->saved_character = vm->character;
        vm->mode = vm->mem4b00[0xe6] == 0 ? 3 : 4;
        status = cok_adventure_load(game, 0x39);
        if (status == COK_ECL_OK) status = cok_ecl_run(vm, vm->vectors[4]);
    } else {
        uint8_t block = vm->mem4b00[0xf2] != 0 ? (uint8_t)vm->mem4b00[0xf2] : 0x24;
        status = cok_adventure_enter(game, block);
        if (status == COK_ECL_OK && !vm->abort) status = cok_adventure_play(game);
    }
    if (status != COK_ECL_OK || game->input_ended || game->quit) return status;
    vm->abort = false;
    while (game->party.count > 0) {
        vm->character = game->party.members[0]->record;
        if (!cok_treasure_remove_record(game, 0, keep, true, true)) return vm->status;
    }
    vm->character = NULL;
    return COK_ECL_OK;
}

cok_ecl_status cok_start_game(cok_adventure *game, bool woof)
{
    cok_ecl *vm = &game->vm;
    cok_start_startup(game);
    if (!woof) cok_start_title(game);
    if (vm->abort) return vm->status;
    cok_adventure_log(game, "unported", "the sound driver (17e8:00da)");
    int key = title_menu(game, "Champions of Krynn v1.2");
    if (key < 0) return vm->status;
    game->demo = key == 'D';
    for (;;) {
        vm->file = game->demo ? 2 : 1;
        if (game->demo) {
            game->speed = 9;
        } else {
            cok_start_menu(game);
            if (vm->abort) break;
            /* 1000:024b: main draws the frame and party list again. */
            uint16_t phases[3];
            moons(game, phases);
            if (vm->mode == 3 && vm->mem4b00[0x138] == 0)
                cok_screen_big(&game->screen, &game->view.tiles[4], phases);
            else
                cok_screen_adventure(&game->screen, &game->view.tiles[4], phases);
            cok_adventure_party(game);
        }
        cok_ecl_status status = adventure(game);
        if (status != COK_ECL_OK || vm->abort) return status;
        cok_start_reset(game);
        if (game->demo) {
            cok_start_title(game);
            if (vm->abort) break;
            key = title_menu(game, "Champions Of Krynn v1.2");
            if (key < 0) break;
            game->demo = key == 'D';
        }
    }
    return vm->status;
}
