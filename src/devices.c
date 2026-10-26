/* The mouse and the joystick in the game: their keys (1614:025b, 01ad,
 * 00fa, 03c2), a menu's clicks (67b5:03e2's regions before row 24) and
 * its wait for the buttons (67b5:0c2d), and startup's part (11c7:031a,
 * 3e99:005b). See input.h for the units over the machine. */
#include "adventure.h"

#include <string.h>

void cok_adventure_devices(cok_adventure *game, bool mouse, bool joystick,
                           const uint16_t x[7], const uint16_t y[7])
{
    /* 11c7:0aa8: the third line, then 177c:0000 (11c7:0b16), and with the
     * stick still on, its thresholds. */
    game->mouse.on = mouse;
    game->stick.on = joystick;
    cok_stick_init(&game->stick, &game->port);
    if (game->stick.on) {
        memcpy(game->stick.x, x, sizeof game->stick.x);
        memcpy(game->stick.y, y, sizeof game->stick.y);
    }
}

void cok_adventure_mouse_startup(cok_adventure *game)
{
    /* 3e99:0085-0134: with KRYNN.CFG's M, the driver found or not; then
     * the click's place at the middle and the pointer moved there. */
    if (!game->mouse.on) return;
    cok_mouse_start(&game->mouse, game->display == COK_DISPLAY_CGA);
    game->pointer.x = 0xa0;
    game->pointer.y = 100;
    cok_mouse_set(&game->mouse, game->pointer.x, game->pointer.y);
}

bool cok_adventure_devices_on(const cok_adventure *game)
{
    return game->mouse.on || game->stick.on;
}

bool cok_adventure_device_ready(cok_adventure *game)
{
    if (game->device_wait == COK_WAIT_MOUSE) return game->mouse.driver.buttons != 0;
    if (game->device_wait != COK_WAIT_STICK) return false;
    uint16_t buttons;
    uint8_t direction;
    cok_stick_poll(&game->stick, &game->port, &direction, &buttons);
    return direction < 8 || buttons != 0;
}

int cok_adventure_device_key(cok_adventure *game)
{
    if (game->mouse.on) {
        /* 1614:01ad: the left button Enter, the right Escape, as a click.
         * Other buttons (both, the middle) leave 025b's key unset, which
         * from a menu (03c2's frame under it) holds 0: so 0 here. */
        unsigned buttons = 0;
        cok_mouse_get(&game->mouse, &buttons, &game->pointer.x, &game->pointer.y);
        if (buttons == 0) return -1;
        game->pointer.clicked = true;
        return buttons == 1 ? 0x0d : buttons == 2 ? 0x1b : 0;
    }
    /* 1614:00fa: a direction its keypad digit, else button 1 Enter or 2
     * Escape. */
    uint8_t direction;
    uint16_t buttons;
    cok_stick_poll(&game->stick, &game->port, &direction, &buttons);
    if (direction < 8) return cok_stick_key(direction);
    if (buttons == 1) return 0x0d;
    if (buttons == 2) return 0x1b;
    return -1;
}

bool cok_adventure_device_pending(cok_adventure *game)
{
    /* 1614:03c2's devices: the mouse's position kept, a button down; the
     * stick's button or push. */
    if (game->mouse.on) {
        unsigned buttons = 0;
        cok_mouse_get(&game->mouse, &buttons, &game->pointer.x, &game->pointer.y);
        return buttons != 0;
    }
    if (game->stick.on) {
        uint8_t direction;
        uint16_t buttons;
        cok_stick_poll(&game->stick, &game->port, &direction, &buttons);
        return (int16_t)buttons > 0 || direction < 8;
    }
    return false;
}

void cok_adventure_device_wait(cok_adventure *game, unsigned ms)
{
    if (game->hooks.device_wait != NULL)
        game->hooks.device_wait(game, ms, game->hooks.context);
}

uint8_t *cok_adventure_special(cok_adventure *game, uint8_t scan)
{
    if (game->mouse.on && game->pointer.clicked) {
        /* dkk 580d:2bb2: the member a click on the party list kept
         * (6a69:0513, DS:a2b6) selected, once. */
        if (game->click_pick != NULL) {
            uint8_t *p = game->click_pick;
            game->click_pick = NULL;
            if (cok_party_index(&game->party, p) >= game->party.count) {
                cok_adventure_fail(game, COK_ECL_UNDEFINED,
                                   "a special key selects a click's member the party does not "
                                   "have (dkk 580d:2bb2, DS:a2b6)");
                return game->vm.character;
            }
            return p;
        }
        /* 546c:3334: a click on the party list has picked already (dkk:
         * without one kept, a key as any other). */
        if (!cok_game_rules(game)->menus.special_keeps) return game->vm.character;
    }
    /* dkk 580d:2bb2: a key but Up and Down keeps the one selected. */
    if (cok_game_rules(game)->menus.special_keeps && scan != 0x48 && scan != 0x50)
        return game->vm.character;
    if (cok_ecl_selection_freed(&game->vm) && game->party.count > 0) {
        /* The selection a record freed (see ecl.h): 546c:3334's Up looks
         * for the member before it, finds none and selects nil; Down reads
         * its next field, the freed bytes; others the first. */
        if (scan == 0x48 && !cok_game_rules(game)->menus.special_keeps) {
            game->vm.selection_freed = false;
            return NULL;
        }
        if (scan == 0x48 || scan == 0x50) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED,
                               "%s reads the next field of the selected record, one removed "
                               "and freed since (4def:3b0a), restored from DS:43bf",
                               cok_game_rules(game)->menus.special_keeps ? "dkk 580d:2bb2"
                                                                         : "546c:3334");
            return NULL;
        }
    }
    return cok_party_special(&game->party, game->vm.character, scan);
}

/* DS:1ff5[row * 11 + col] for rows and columns 3-13 (the 3D view's
 * cells): forward in the upper triangle, the turns at the sides, about
 * below. */
static const uint8_t view_keys[11][11] = {
    {0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48},
    {0x4b, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x4d},
    {0x4b, 0x4b, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x4d, 0x4d},
    {0x4b, 0x4b, 0x4b, 0x48, 0x48, 0x48, 0x48, 0x48, 0x4d, 0x4d, 0x4d},
    {0x4b, 0x4b, 0x4b, 0x4b, 0x48, 0x48, 0x48, 0x4d, 0x4d, 0x4d, 0x4d},
    {0x4b, 0x4b, 0x4b, 0x4b, 0x4b, 0x48, 0x4d, 0x4d, 0x4d, 0x4d, 0x4d},
    {0x4b, 0x4b, 0x4b, 0x4b, 0x50, 0x50, 0x50, 0x4d, 0x4d, 0x4d, 0x4d},
    {0x4b, 0x4b, 0x4b, 0x50, 0x50, 0x50, 0x50, 0x50, 0x4d, 0x4d, 0x4d},
    {0x4b, 0x4b, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x4d, 0x4d},
    {0x4b, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x4d},
    {0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50},
};

/* The combat map's region (077d; dkk 0680): in Move or with Manual's
 * cursor, cells 1-21 of the battlefield. */
static bool combat_click(const cok_adventure *game, int col, int row)
{
    return (game->combat_moving || game->aiming) && game->vm.mode == 5 && col >= 1 &&
           col <= 21 && row >= 1 && row <= 21;
}

/* Its key (0794-0876; dkk 06d9): a step from the cursor (Manual's) or the
 * mover toward the cell clicked, in 24-pixel cells from cell 1. */
static int combat_step(const cok_adventure *game, int col, int row)
{
    const cok_combat *combat = &game->combat;
    int fx, fy;
    if (game->aiming) {
        fx = game->aim_x - combat->view_x + 1;
        fy = game->aim_y - combat->view_y + 1;
    } else {
        const cok_combatant *c = &combat->combatant[cok_combat_index(combat, game->vm.character)];
        fx = c->x - combat->view_x + 1;
        fy = c->y - combat->view_y + 1;
    }
    return cok_mouse_direction((int16_t)fx, (int16_t)fy, (int16_t)(col / 3 + 1),
                               (int16_t)(row / 3 + 1));
}

/* Modify's sheet (09c1-0a35; dkk 0ac5): Home, the field clicked kept in
 * 6e35 (dkk c11e); other rows give nothing. */
static int sheet_click(cok_adventure *game, int col, int row, bool *special)
{
    if (row == 1) {
        if (col >= 1 && col <= 16) game->modify_field = 6;
    } else if (row == 3) {
        if (col >= 31 && col <= 33) game->modify_field = 7;
    } else if (row >= 9 && row <= 14) {
        if (col >= 5 && col <= 9) game->modify_field = (uint8_t)(row - 9);
    } else {
        return COK_CLICK_KEEP;
    }
    *special = true;
    return 0x47;
}

/* 67b5:03e2's click (065c-0a35), the regions before row 24 and Modify's
 * after it. */
static int click(void *context, int col, int row, bool *special)
{
    cok_adventure *game = context;
    cok_ecl *vm = &game->vm;
    /* The party list (065c-077a): the member on the row picked, as Up. */
    if (game->mouse.party_clicks && !(vm->mode == 3 && !cok_ecl_flag(vm, COK_VAR_OFF_MAP)) &&
        !game->moving && vm->mode != 5 && col >= game->party_left && col <= game->party_right &&
        row >= game->party_top && row <= game->party_bottom) {
        uint8_t *p = cok_party_record(&game->party, (uint8_t)(row - game->party_top));
        /* A row past the party: taken, nothing given (077a). */
        if (p == NULL) return COK_CLICK_KEEP;
        if (game->pick_into)
            game->pick = p;
        else
            vm->character = p;
        *special = true;
        return 0x48;
    }
    /* The combat map (077d-0876). */
    if (combat_click(game, col, row)) {
        *special = true;
        return combat_step(game, col, row);
    }
    /* Move mode (0879-0917): the 3D view's cells 3-13, or the overland
     * map's, from the party's place on it. */
    if (game->moving && ((col >= 3 && col <= 13 && row >= 3 && row <= 13) ||
                         (cok_ecl_var(vm, COK_VAR_3D) == 0 && vm->mode == 3 && col >= 1 &&
                          col <= 38 && row >= 1 && row <= 15))) {
        *special = true;
        if (cok_ecl_var(vm, COK_VAR_3D) == 0)
            return cok_mouse_direction((int16_t)(cok_ecl_var(vm, COK_VAR_OVERLAND_X) + 1),
                                       (int16_t)(cok_ecl_var(vm, COK_VAR_OVERLAND_Y) + 1),
                                       (int16_t)col, (int16_t)row);
        return view_keys[row - 3][col - 3];
    }
    /* Row 24 is the menu's (091a). */
    if (row == 24) return COK_CLICK_ON;
    /* Modify's sheet (09c1-0a35). */
    if (game->modifying) return sheet_click(game, col, row, special);
    return COK_CLICK_ON;
}

/* dkk 6a69:0f1b's regions by stage (0490-08bb, 0a9b, 1271-12c2): as
 * 67b5:03e2's but the party list's member is the special key's to select
 * (DS:a2b6, 580d:2bb2), or the pick's (DS:c0f0, c0f1: the pick into),
 * and a click on the one selected or picked is Enter on the item selected;
 * Move mode keeps the party list's clicks but in a 3D area's mode (4), and
 * its view takes them only there; a click on none of them, on the
 * battlefield (mode 5, rows 0-22) is T on a menu of two rows and M on
 * aiming's (its prompt, DS:3109), elsewhere Enter on a menu of one item,
 * which has no such hotkey. */
static int click_indexed(void *context, int stage, int col, int row, const cok_click_menu *menu,
                         bool *special)
{
    cok_adventure *game = context;
    cok_ecl *vm = &game->vm;
    if (stage == COK_CLICK_SHEET) {
        if (game->modifying) return sheet_click(game, col, row, special); /* 0a9b */
        return COK_CLICK_ON;
    }
    if (stage == COK_CLICK_ELSEWHERE) {
        if (vm->mode == 5 && row <= 0x16) {
            const char *aim = cok_game_rules(game)->commands.aim_prompt;
            *special = false;
            if (menu->two_rows) return 'T';
            if (aim != NULL && strcmp(menu->prompt, aim) == 0) return 'M';
            return COK_CLICK_KEEP;
        }
        /* 12af: Escape set but not taken, read on; or Enter, done. */
        if (menu->count > 1) return COK_CLICK_KEEP;
        *special = false;
        return 0x0d;
    }
    /* The party list (0490, 0513). */
    if (game->mouse.party_clicks && !(vm->mode == 3 && !cok_ecl_flag(vm, COK_VAR_OFF_MAP)) &&
        !(game->moving && vm->mode == 4) && vm->mode != 5 && col >= game->party_left &&
        col <= game->party_right && row >= game->party_top && row <= game->party_bottom) {
        uint8_t *p = cok_party_record(&game->party, (uint8_t)(row - game->party_top));
        if (p == NULL) return COK_CLICK_KEEP;
        if (game->pick_into) {
            if (p == game->pick) return COK_CLICK_ENTER;
            game->pick = p;
        } else {
            if (p == vm->character) return COK_CLICK_ENTER;
            game->click_pick = p;
        }
        *special = true;
        return 0x48;
    }
    /* The combat map (0680, 06d9). */
    if (combat_click(game, col, row)) {
        *special = true;
        return combat_step(game, col, row);
    }
    /* The 3D view in Move mode (07d3, 0825: DS:57a7, 1ff5's). */
    if (game->moving && col >= 3 && col <= 13 && row >= 3 && row <= 13 && vm->mode == 4) {
        *special = true;
        return view_keys[row - 3][col - 3];
    }
    /* The overland map in Move mode (085d, 08bb). */
    if (game->moving && cok_ecl_var(vm, COK_VAR_3D) == 0 && vm->mode == 3 && col >= 1 &&
        col <= 38 && row >= 1 && row <= 15) {
        *special = true;
        return cok_mouse_direction((int16_t)(cok_ecl_var(vm, COK_VAR_OVERLAND_X) + 1),
                                   (int16_t)(cok_ecl_var(vm, COK_VAR_OVERLAND_Y) + 1),
                                   (int16_t)col, (int16_t)row);
    }
    return COK_CLICK_ON;
}

/* 67b5:0c2d: after a click, until the mouse's buttons are up; with the
 * joystick, after any key, until its buttons are. */
static void release(void *context)
{
    cok_adventure *game = context;
    if (game->pointer.clicked) {
        for (;;) {
            unsigned buttons = 0;
            int16_t x, y;
            cok_mouse_get(&game->mouse, &buttons, &x, &y);
            if (buttons == 0 || game->vm.abort) return;
            cok_adventure_device_wait(game, 10);
        }
    } else if (game->stick.on) {
        for (;;) {
            uint8_t direction;
            uint16_t buttons;
            cok_stick_poll(&game->stick, &game->port, &direction, &buttons);
            if (buttons == 0 || game->vm.abort) return;
            cok_adventure_device_wait(game, COK_STICK_READ_MS);
        }
    }
}

static void hide(void *context)
{
    cok_adventure *game = context;
    cok_mouse_hide(&game->mouse);
}

void cok_adventure_pointer_init(cok_adventure *game)
{
    game->pointer.click = click;
    game->pointer.click_indexed = click_indexed;
    game->pointer.release = release;
    game->pointer.hide = hide;
    game->pointer.context = game;
    game->modify_field = 0; /* DS:6e35, set by Modify (4def:29d8) before its use */
}

/* 1521:03e2: Delay(DS:4b38 * 3) after each character while DS:4b59 is
 * set. */
static void typed(void *context)
{
    cok_adventure *game = context;
    if (game->text_delay) cok_adventure_wait(game, game->speed * 3u);
}

void cok_adventure_wrap(cok_adventure *game, cok_text_cursor *cursor, const char *text,
                        cok_text_window window, uint8_t fg, bool clear,
                        void (*page)(void *context))
{
    cok_text_hooks typing = {page, typed, game};
    cok_mouse_hide(&game->mouse);
    cok_text_wrap(&game->screen, &game->font, cursor, text, window, fg, 0, clear, &typing);
    /* 1521:04e4: left, top, right or bottom past 0x27, 0x18, 0x27, 0x27
     * leave without the show. */
    if (window.left > 0x27 || window.top > 0x18 || window.right > 0x27 || window.bottom > 0x27)
        return;
    cok_mouse_show(&game->mouse);
}
