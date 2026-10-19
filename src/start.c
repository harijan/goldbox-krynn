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

const char *cok_start_item(const cok_rules *rules, unsigned i)
{
    return i < 13 ? rules->start.items[i] : NULL;
}

static void clear_menu(cok_adventure *game)
{
    cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 67b5:0c7b */
}

bool cok_start_pick(cok_adventure *game)
{
    /* 546c:36d2: a click on the party list picks while this asks. */
    bool party_clicks = game->mouse.party_clicks;
    game->mouse.party_clicks = true;
    int key;
    for (;;) {
        bool special;
        key = cok_camp_menu(game, cok_game_rules(game)->start.pick_prompt,
                            cok_game_rules(game)->start.pick_menu, true, false, &special);
        if (key < 0) {
            game->mouse.party_clicks = party_clicks;
            return false;
        }
        if (special) {
            cok_camp_pick(game, (uint8_t)key);
            continue;
        }
        if (key == 0 || key == 'E' || key == 'S') break;
    }
    cok_adventure_party(game);
    game->mouse.party_clicks = party_clicks; /* 546c:3764 */
    return key == 'S';
}

/* The state both 3e99:005b and 3e99:0843 leave (dkk 4f9a:00e1, 08ad): the
 * wall table's entry wall (0, 1) and those after it none. */
static void fresh(cok_adventure *game, uint8_t facing, uint8_t wall)
{
    cok_ecl *vm = &game->vm;
    memset(vm->mem4b00, 0, sizeof vm->mem4b00);
    memset(vm->mem7c00, 0, sizeof vm->mem7c00);
    memset(vm->mem7a00, 0, sizeof vm->mem7a00);
    cok_ecl_set_var(vm, COK_VAR_DIFFICULTY, 3); /* difficulty */
    cok_ecl_set_var(vm, COK_VAR_3D, 1);         /* a 3D area */
    for (size_t i = 0; i < 3; ++i) cok_ecl_set_var_at(vm, COK_VAR_MOONS, i, 2);
    vm->map_x = 7;
    vm->map_y = 13;
    vm->direction = facing;
    vm->ahead = vm->square = 0;
    for (size_t i = 0; i < 3; ++i) game->door_tries[i] = true;
    for (size_t i = wall; i < COK_VIEW_WALL_SLOTS; ++i)
        game->wall_ids[i] = game->wall_slots[i] = -1;
    game->wall_ids[wall] = 0;
    game->wall_slots[wall] = 1;
    game->vault_letter = 'Q'; /* dkk DS:fb79, fb78 */
    game->vault_temp = false;
    vm->keep_vars = false;
    game->rest_ticks = 0;
    vm->character = NULL;
    vm->file = 1;
    game->speed = 4;
    game->text_delay = true; /* 3e99:0560, 099d */
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
    /* 3e99:0666-067f, 0a86-0a9f: no Move, aiming cursor, click, pick or
     * Modify (DS:71a6, 71a5, 8987, 6e08, 7141). */
    game->combat_moving = game->aiming = false;
    game->pointer.clicked = false;
    game->pick_into = false;
    game->modifying = false;
}

void cok_start_startup(cok_adventure *game)
{
    cok_adventure_mouse_startup(game); /* 3e99:0085 */
    fresh(game, 0, 0);
    game->debug = false; /* 3e99:055b */
    game->demo = false;
    /* 3e99:06a4: "Loading...Please Wait" while the tiles, the sky, the
     * combat sprites and the item types load; the title draws over it. */
    cok_adventure_loading(game);
}

void cok_start_reset(cok_adventure *game)
{
    fresh(game, 2, cok_game_rules(game)->start.reset_wall);
    cok_debug_reset(game); /* 3e99:0977 */
    game->mouse.party_clicks = true; /* 3e99:0933 */
    clear_menu(game);
}

/* 2f55:0000: flush the keyboard, then wait hundredths, polling each
 * hundredth (GetTime's), unless a key is pressed, which is read; then flush
 * it again (1614:045c), so that the key and any others are thrown away.
 * Returns true for a key. */
static bool title_wait(cok_adventure *game, unsigned hundredths)
{
    cok_adventure_flush(game);
    for (unsigned waited = 0; waited < hundredths; ++waited) {
        if (cok_adventure_key_pending(game)) {
            cok_keyboard keys = cok_adventure_keyboard(game);
            keys.read(keys.context);
            cok_adventure_flush(game);
            return true;
        }
        if (game->vm.abort) return false;
        cok_adventure_wait(game, 10);
    }
    cok_adventure_flush(game); /* 2f55:005f, the time passed too */
    return false;
}

static bool title_picture(cok_adventure *game, uint8_t id, int x, int y)
{
    cok_picture picture = {0};
    if (!cok_adventure_load_image(game, cok_game_rules(game)->archives.title, id, -1, &picture)) {
        cok_adventure_fail(game, COK_ECL_LOAD_FAILED, "%s", game->error);
        return false;
    }
    cok_picture_draw(&game->screen, &picture, 0, x, y, 0, NULL); /* 127f:10e7 */
    cok_picture_free(&picture);
    return true;
}

/* The credits (2f55:0267; dkk 35c6:026e): the frame's, then the game's
 * lines. */
static void credits(cok_adventure *game)
{
    const cok_rules_start *start = &cok_game_rules(game)->start;
    cok_adventure_screen(game, COK_SCREEN_CREDITS); /* 1128:0130 */
    for (size_t i = 0; i < start->credit_count; ++i) {
        const cok_rules_text_line *line = &start->credits[i];
        cok_adventure_log(game, "print", line->text);
        cok_text_string(&game->screen, &game->font, line->text, line->x, line->y, line->fg, 0);
    }
}

void cok_start_title(cok_adventure *game)
{
    const cok_rules_start *start = &cok_game_rules(game)->start;
    game->title = true;
    cok_adventure_free_picture(game); /* 6961:0537 */
    /* 2f55:063c: DS:5780 set and the music started (1661:000a, 02e6,
     * 014d, 02b3) unless the sound is off. */
    cok_adventure_log(game, "sound", "music");
    cok_adventure_sound(game, COK_SOUND_MUSIC, 0);
    /* 2f55:0651: the mouse's cursor hidden through the title, its waits
     * too, until 2f55:006d (0086). */
    cok_mouse_hide(&game->mouse);
    bool key = false;
    for (size_t i = 0; i < start->picture_count && !key; ++i) {
        const cok_rules_title_picture *p = &start->pictures[i];
        if (p->clear) cok_picture_fill(&game->screen, 0, 0, 40, 200, 0); /* dkk 14e4:0a37 */
        key = !title_picture(game, p->record, p->x, p->y) ||
              (p->seconds > 0 && title_wait(game, p->seconds * 100u));
    }
    if (!key || start->credits_always) {
        cok_picture_fill(&game->screen, 0, 0, 40, 200, 0); /* 1521:0a34 */
        credits(game);
        title_wait(game, start->credits_wait);
    }
    /* 2f55:006d: the music stopped (1661:0353, 00da), DS:5780 cleared. */
    cok_picture_fill(&game->screen, 0, 0, 40, 200, 0);
    cok_adventure_sound(game, COK_SOUND_MUSIC_END, 0);
    cok_mouse_show(&game->mouse);
    game->title = false;
}

/* The title's menu (1000:0165, 0335): Play or Demo, which the time running
 * out with no key picks (30 seconds, after a demonstration 10: DS:6e11);
 * so does F10, whose scan code is 0x44, D. */
static int title_menu(cok_adventure *game, const char *prompt, uint16_t timeout)
{
    cok_adventure_log(game, "menu", "Play Demo");
    cok_keyboard keys = cok_adventure_keyboard(game);
    game->timed = game->title = true;
    game->timeout = timeout;
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

/* KRYNN.CFG written again by Initialize Mouse/Joystick, its third line
 * input and the stick's thresholds (or zeros), in the port's own place,
 * never the assets'. */
static void rewrite(cok_adventure *game, char input, const cok_stick *stick)
{
    char text[1100];
    if (game->config_write[0] == '\0') {
        snprintf(text, sizeof text, "line 3 %c, written nowhere", input);
    } else if (cok_config_rewrite(game->config_path, game->config_write, input,
                                  stick != NULL ? stick->x : NULL,
                                  stick != NULL ? stick->y : NULL)) {
        snprintf(game->config_path, sizeof game->config_path, "%s", game->config_write);
        snprintf(text, sizeof text, "line 3 %c", input);
    } else {
        snprintf(text, sizeof text, "%s cannot be written", game->config_write);
    }
    cok_adventure_log(game, "config", text);
}

/* 4def:638d's screens: the window cleared (1128:07e6) and its lines in
 * light magenta at column 1 (1521:0353); the failure's for 2 seconds. */
static void calibration_prompt(cok_stick_prompt which, void *context)
{
    static const char *const lines[3][2] = {
        {"Push JOYSTICK #1 to the extreme upper", "left position and press button #1."},
        {"Push JOYSTICK #1 to the extreme lower", "right position and press button #1."},
        {"Unable to read joystick", NULL},
    };
    cok_adventure *game = context;
    cok_picture_fill(&game->screen, 1, 8, 0x26, 0x16 * 8, 0);
    for (size_t i = 0; i < 2 && lines[which][i] != NULL; ++i) {
        cok_adventure_log(game, "print", lines[which][i]);
        cok_text_string(&game->screen, &game->font, lines[which][i], 1, 3 + 2 * (int)i, 13, 0);
    }
    if (which == COK_STICK_FAILED) cok_adventure_wait(game, 2000);
}

void cok_start_mouse(cok_adventure *game)
{
    cok_menu_style style = {"Init Mouse/Joystick: ", "Select", 15, 10, 13, true};
    int index = 0;
    cok_keyboard keys = cok_adventure_keyboard(game);
    for (;;) {
        /* 4def:5d53: the rows say what is on now. */
        cok_menu_row rows[3] = {{game->mouse.on ? "mouse ON" : "mouse OFF", false},
                                {game->stick.on ? "joystick ON" : "joystick OFF", false},
                                {"disable both", false}};
        bool redraw = true;
        cok_adventure_log(game, "list", "Init Mouse/Joystick: ");
        for (size_t i = 0; i < 3; ++i) cok_adventure_log(game, "item", rows[i].text);
        int key = cok_menu_rows(&game->screen, &game->font, rows, 3,
                                (cok_text_window){9, 12, 0x26, 0x16}, &style, &redraw, &index,
                                &game->list_top, &game->selected, &keys);
        if (key != 'S') return;
        if (index == 0) {
            /* 4def:5e8e: the mouse turned over; on, its driver found
             * (cok_mouse_start), the joystick off and M; else N, the
             * cursor hidden (the flag set for it) and the mouse off. */
            game->mouse.on = !game->mouse.on;
            if (game->mouse.on) {
                cok_mouse_start(&game->mouse, game->display == COK_DISPLAY_CGA);
                if (game->mouse.on) {
                    game->stick.on = false;
                    rewrite(game, 'M', NULL);
                    game->pointer.x = 0xa0; /* no move: the pointer stays at 639, 100 */
                    game->pointer.y = 100;
                }
            }
            if (!game->mouse.on) {
                rewrite(game, 'N', NULL);
                game->mouse.on = true;
                cok_mouse_hide(&game->mouse);
                game->mouse.on = false;
            }
        } else if (index == 1) {
            /* 4def:6087: the joystick turned over; on, calibrated in the
             * window (4def:638d), then J and its thresholds, the mouse off;
             * off, or not found, N, the mouse as it was. */
            game->stick.on = !game->stick.on;
            if (game->stick.on) {
                cok_stick_calibrate(&game->stick, &game->port, calibration_prompt, game,
                                    &game->vm.abort);
                if (game->vm.abort) return;
                if (game->stick.on) {
                    rewrite(game, 'J', &game->stick);
                    cok_mouse_hide(&game->mouse);
                    game->mouse.on = false;
                }
            }
            if (!game->stick.on) rewrite(game, 'N', NULL);
        } else if (index == 2) {
            /* 4def:621d: both off. */
            game->stick.on = false;
            cok_mouse_hide(&game->mouse);
            game->mouse.on = false;
            rewrite(game, 'N', NULL);
        }
    }
}

/* Begin Adventuring (4def:04b2): with a party, the mode back as it was,
 * the frame and party list drawn unless a game was loaded here and the
 * first block will draw them, row 24 cleared and var 0x7ea8 cleared. A
 * game loaded here gets the game's frame (cok_rules_frame's resumed):
 * Champions' the adventure frame whatever the mode, in block 0 or off the
 * map (4def:01b4); Death Knights' by the mode (dkk 49cb:010b). */
static bool begin(cok_adventure *game, uint8_t mode)
{
    cok_ecl *vm = &game->vm;
    if (game->party.count == 0 && !game->demo) return false;
    /* dkk 49cb:010b: the last mode (DS:8c4f), as a load may have set it. */
    vm->mode = cok_game_rules(game)->start.begin_last_mode ? vm->last_mode : mode;
    if (!vm->keep_vars || game->picture_id == 9) {
        if (vm->mode == 3 && !cok_ecl_flag(vm, COK_VAR_OFF_MAP))
            cok_adventure_screen(game, COK_SCREEN_BIG); /* 1128:0344 */
        else
            cok_adventure_screen(game, COK_SCREEN_ADVENTURE); /* 1128:0242 */
        cok_adventure_party(game);
    } else if (cok_game_rules(game)->frame.resumed == COK_RESUMED_BY_MODE) {
        if (vm->mode == 0 || vm->mode == 4) {
            cok_adventure_screen(game, COK_SCREEN_ADVENTURE);
            cok_adventure_party(game);
        } else if (vm->mode == 3) {
            cok_adventure_screen(game, COK_SCREEN_BIG);
        }
    } else if (cok_ecl_var(vm, COK_VAR_BLOCK) == 0 || cok_ecl_flag(vm, COK_VAR_OFF_MAP)) {
        cok_adventure_screen(game, COK_SCREEN_ADVENTURE);
        cok_adventure_party(game);
    }
    clear_menu(game);
    cok_ecl_set_var(vm, COK_VAR_HALL, 0);
    return true;
}

/* Exit to DOS (4def:055a; dkk 49cb:010b's, which quits once No has
 * saved). */
static void leave(cok_adventure *game)
{
    const cok_rules_start *start = &cok_game_rules(game)->start;
    int answer = cok_camp_yes_no(game, start->quit, start->ask_colour);
    if (answer != 'Y') return;
    if (game->party.count > 0 && game->effects.rolls.saved == 0) {
        answer = cok_camp_yes_no(game, start->unsaved, start->ask_colour);
        if (answer < 0) return;
        if (answer == 'N') {
            cok_camp_save(game);
            if (!start->quit_after_save || game->vm.abort) return;
        }
    }
    cok_camp_quit(game);
}

/* A command the port does not carry out yet for the game: the run stops
 * there, citing the game's routine. */
static bool unported(cok_adventure *game, unsigned item)
{
    const cok_rules *rules = cok_game_rules(game);
    const char *routine = rules->start.unported[item];
    if (routine == NULL) return false;
    cok_adventure_fail(game, COK_ECL_UNPORTED, "not yet ported: %s's %s (%s)", rules->text.name,
                       rules->start.items[item], routine);
    return true;
}

/* A Drop or Remove that left no one ends the game there, where the game's
 * does (dkk 49cb:010b: 4f9a:08ad, then DS:8c5b): the end of a game's
 * state, the mode the menu returns to the party menu's, and the adventure
 * starting over once the menu returns (cok_adventure_play). */
static void emptied(cok_adventure *game, uint8_t *entry)
{
    if (!cok_game_rules(game)->start.reset_when_empty || game->party.count > 0) return;
    cok_start_reset(game);
    cok_ecl *vm = &game->vm;
    vm->last_mode = vm->mode; /* 49cb:010b's start again */
    vm->mode = 0;
    *entry = vm->last_mode;
    game->free_training = false; /* DS:c46a */
    game->new_game = true;
    vm->halt = true; /* DS:8c5b: the script that opened the menu stops too */
}

/* The items each pass offers (4def:01ef): by whether a character is
 * selected; Train and Knight Change Classes also need a hall (var 0x7ea8)
 * or free training. */
static void flags(cok_adventure *game, bool on[13])
{
    bool any = game->vm.character != NULL;
    bool hall = any && (cok_ecl_var(&game->vm, COK_VAR_HALL) != 0 || game->free_training);
    bool want[13] = {true, any, any, hall, hall, any, true, any, !any, any, any, !any, true};
    memcpy(on, want, sizeof want);
}

/* The mode the start menu was entered with ([bp-0x10]), for Begin. */
static uint8_t entry_mode;

bool cok_start_command(cok_adventure *game, char letter)
{
    bool on[13];
    flags(game, on);
    /* The item the letter begins: the original takes the row's first
     * letter (dkk the row's place, the same: the items' letters differ). */
    unsigned item = 13;
    for (unsigned i = 0; i < 13; ++i)
        if (cok_game_rules(game)->start.items[i][0] == letter) item = i;
    if (item < 13 && on[item] && unported(game, item)) return false;
    switch (letter) {
    case 'C': if (on[0]) cok_create(game); break;
    case 'D':
        if (!on[1]) break;
        cok_roster_drop(game);
        emptied(game, &entry_mode);
        break;
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
    case 'R':
        if (!on[7]) break;
        cok_roster_remove(game);
        emptied(game, &entry_mode);
        break;
    case 'L': if (on[8]) cok_roster_load(game); break;
    case 'S': if (on[9] && game->party.count > 0) cok_camp_save(game); break;
    case 'B':
        if (!on[10]) break;
        game->mouse.party_clicks = true; /* 4def:04c3 */
        return begin(game, entry_mode);
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
    const cok_rules_start *start = &cok_game_rules(game)->start;
    uint8_t outer = entry_mode;
    entry_mode = vm->mode;
    /* dkk 49cb:010b keeps the mode in the last mode (DS:8c4f), Begin's. */
    if (start->begin_last_mode) vm->last_mode = vm->mode;
    vm->mode = 0;
    game->free_training = false;
    game->mouse.party_clicks = false; /* 4def:01bb */
    int index = 0;
    for (;;) {
        if (vm->abort) break;
        cok_adventure_screen(game, COK_SCREEN_OPEN); /* 1128:0000 */
        if (vm->character != NULL) cok_adventure_party(game);
        bool on[13];
        flags(game, on);
        cok_menu_row rows[13];
        size_t count = 0;
        cok_adventure_log(game, "list", start->prompt);
        for (size_t i = 0; i < 13; ++i) {
            if (!on[i]) continue;
            rows[count++] = (cok_menu_row){start->items[i], false};
            cok_adventure_log(game, "item", start->items[i]);
        }
        cok_menu_style style = {start->prompt, start->select, 15, 10, 13, false};
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
        bool ended = game->new_game;
        if (cok_start_command(game, letter)) break;
        /* dkk 49cb:010b: the game ended, the list starts on its first row. */
        if (game->new_game && !ended) index = 0;
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
    const cok_rules_start *start = &cok_game_rules(game)->start;
    if (game->demo) {
        /* As 2fd3:3c28 starts, with no party list. */
        game->redraw = true;
        game->files_loaded = game->pieces_loaded = game->map_loaded = false;
        game->frame_pending = false;
        vm->saved_character = vm->character;
        vm->mode = cok_ecl_var(vm, COK_VAR_3D) == 0 ? 3 : 4;
        status = cok_adventure_load(game, start->demo_block);
        if (status == COK_ECL_OK) status = cok_ecl_run(vm, vm->vectors[4]);
    } else {
        /* A start menu that ended the game (dkk DS:8c5b) starts it again
         * from a new game's block (3632:2e06's outer loop). */
        do {
            game->new_game = false;
            vm->halt = false; /* 3632:2e0e */
            uint8_t block = cok_ecl_var(vm, COK_VAR_BLOCK) != 0
                                ? (uint8_t)cok_ecl_var(vm, COK_VAR_BLOCK)
                                : start->first_block;
            status = cok_adventure_enter(game, block);
            if (status == COK_ECL_OK && !vm->abort && !game->new_game)
                status = cok_adventure_play(game);
        } while (status == COK_ECL_OK && !vm->abort && !game->input_ended && !game->quit &&
                 game->new_game);
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

void cok_start_redraw(cok_adventure *game)
{
    cok_ecl *vm = &game->vm;
    if (cok_game_rules(game)->frame.menu_big && vm->mode == 3 && !cok_ecl_flag(vm, COK_VAR_OFF_MAP))
        cok_adventure_screen(game, COK_SCREEN_BIG);
    else
        cok_adventure_screen(game, COK_SCREEN_ADVENTURE);
    cok_adventure_party(game);
}

cok_ecl_status cok_start_game(cok_adventure *game, bool woof)
{
    cok_ecl *vm = &game->vm;
    cok_start_startup(game);
    game->woof = woof;
    if (!woof) cok_start_title(game);
    if (vm->abort) return vm->status;
    /* 1000:0160: the effects driver hooked (17e8:00da), logged where the
     * port logged it unported. */
    cok_adventure_log(game, "sound", "install");
    cok_adventure_sound(game, COK_SOUND_INSTALL, 0);
    int key = title_menu(game, cok_game_rules(game)->text.version, 3000);
    if (key < 0) return vm->status;
    game->demo = key == 'D';
    for (;;) {
        vm->file = game->demo ? cok_game_rules(game)->start.demo_file : 1;
        cok_adventure_overlay(game); /* 1000:01d8, around 4def:0000 */
        if (game->demo) {
            game->speed = 9;
        } else {
            cok_start_menu(game);
            if (vm->abort) break;
            cok_start_redraw(game);
        }
        cok_adventure_overlay(game); /* 1000:0282, around 6961:0890 */
        cok_adventure_overlay(game); /* 1000:02b8, around 2fd3:0000 */
        cok_ecl_status status = adventure(game);
        /* Quitting to DOS in the adventure (camp's Quit, Ctrl-C) halts the
         * program there (1614:0000); the adventure loop clears the abort. */
        if (status != COK_ECL_OK || vm->abort || game->quit) return status;
        cok_adventure_overlay(game); /* 1000:02de, around 3e99:0000 */
        cok_start_reset(game);
        if (game->demo) {
            cok_adventure_sound(game, COK_SOUND_COMMAND, 0xff); /* 1000:0304 */
            cok_start_title(game);
            if (vm->abort) break;
            cok_adventure_sound(game, COK_SOUND_INSTALL, 0); /* 1000:030e */
            cok_adventure_flush(game); /* 1000:0313 */
            key = title_menu(game, cok_game_rules(game)->text.version_again, 1000);
            if (key < 0) break;
            game->demo = key == 'D';
            cok_adventure_sound(game, COK_SOUND_COMMAND, 0); /* 1000:036d */
        }
    }
    return vm->status;
}
