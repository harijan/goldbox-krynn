#include "icon.h"

#include "arena.h"
#include "camp.h"
#include "screen.h"

#include <stdio.h>
#include <string.h>

enum { SLOT_NEW = 12 };

static const uint8_t same[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static bool fail(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* 4b6d:0817 without colours. */
static bool build(cok_adventure *game, cok_character *character)
{
    return cok_arena_party_icon(game, character, false);
}

/* 4def:3e20: copy slot from's pictures over slot to's, the sizes of to's,
 * the masks both by the ready picture's size, then with colours give the
 * template's colours the record's (127f:15fd). */
static bool copy(cok_adventure *game, const uint8_t *c, uint8_t from, uint8_t to, bool colours)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_picture *src = game->icons[from], *dst = game->icons[to];
    size_t size = dst[0].frame_size;
    for (int image = 0; image < 2; ++image) {
        if (src[image].pixels == NULL || dst[image].pixels == NULL || src[image].mask == NULL ||
            dst[image].mask == NULL || src[image].frame_size < dst[image].frame_size ||
            src[image].frame_size < size || dst[image].frame_size < size)
            return fail(game, "the icon editor copies icons of other sizes (4def:3e20)");
        memcpy(dst[image].pixels, src[image].pixels, dst[image].frame_size);
        memcpy(dst[image].mask, src[image].mask, size);
    }
    if (!colours) return true;
    /* The template's colours each part's colour byte replaces (DS:390b):
     * body, arm, leg, hair or face, shield and weapon. */
    const cok_data_segment *D = cok_game_rules(game)->data;
    uint8_t map[16];
    memcpy(map, same, sizeof map);
    for (size_t k = 0; k < 6; ++k) {
        uint8_t part = cok_data_u8_at(D, COK_DATA_ICON_COLOURS, k + 1);
        uint8_t colour = cok_field_u8_at(L, c, COK_FIELD_ICON_COLOURS, k + 1);
        map[part] = colour & 15;
        map[part + 8] = colour >> 4;
    }
    for (int image = 0; image < 2; ++image)
        if (cok_picture_recolor(&dst[image], same, map, 0, NULL) != COK_PICTURE_OK)
            return fail(game, "out of memory");
    return true;
}

/* 4def:3d41 (dkk 49cb:39d6): at cell x, y of the view's buffer, 96 by 24
 * pixels cleared, the frame (COMSPR's slot 25; dkk slot 0x1c, the creation
 * section's icon_frame) behind each image, slot 12's ready and attacking
 * images three cells apart (6d21:04b0), and that much shown (127f:12e8),
 * a unit right and a cell down. Nothing else is drawn there, so the port
 * draws on the screen. In CGA mode (4def:3d46) the pixels are filled with
 * colour 3 (CGA's 1, through DS:1e1e) and there is no frame. */
static void pair(cok_adventure *game, int x, int y)
{
    bool cga = game->display == COK_DISPLAY_CGA;
    cok_picture_fill(&game->screen, 3 * x + 1, 24 * y + 8, 12, 24, cga ? 3 : 0);
    const cok_picture *frame = &game->icons[cok_game_rules(game)->creation.icon_frame][0];
    for (int k = 0; k < 2; ++k) {
        int at = 3 * (x + 3 * k) + 1;
        if (frame->pixels != NULL && !cga)
            cok_picture_draw(&game->screen, frame, 0, at, 3 * y + 1, COK_DRAW_MASKED, NULL);
        const cok_picture *icon = &game->icons[SLOT_NEW][k];
        if (icon->pixels != NULL)
            cok_picture_draw(&game->screen, icon, 0, at, 3 * y + 1, COK_DRAW_MASKED, NULL);
    }
}

static void text(cok_adventure *game, const char *s, int x, int y)
{
    cok_text_string(&game->screen, &game->font, s, x, y, 15, 0);
}

static bool edit_indexed(cok_adventure *game, cok_character *character);

bool cok_icon_edit(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_character *character = cok_adventure_selected(game);
    if (character == NULL)
        return fail(game, "the icon editor with no character selected reads through NULL "
                          "(4def:408b)");
    if (cok_game_rules(game)->menus.routine == COK_MENUS_INDEXED)
        return edit_indexed(game, character);
    uint8_t *c = character->record;
    cok_adventure_screen(game, COK_SCREEN_OPEN); /* 1128:0000 */
    static const char *const menus[6] = {
        "", "Parts 1st-color 2nd-color Size Exit", "Head Weapon Exit", "", " Keep Exit",
        "Next Prev Keep Exit",
    };
    /* [bp-0x1a], the level whose key was last taken, is not set before the
     * first: -1 stands for that. */
    int parent = -1;
    for (;;) {
        if (!build(game, character)) return false;
        int level = 1;
        uint8_t colours[6];
        memcpy(colours, c + cok_field_offset(L, COK_FIELD_ICON_COLOURS), sizeof colours);
        uint8_t slot = cok_field_u8(L, c, COK_FIELD_ICON);
        cok_field_set8(L, c, COK_FIELD_ICON, SLOT_NEW);
        bool built = build(game, character);
        cok_field_set8(L, c, COK_FIELD_ICON, slot);
        if (!built) return false;
        uint8_t head = cok_field_u8(L, c, COK_FIELD_ICON_HEAD),
                body = cok_field_u8(L, c, COK_FIELD_ICON_BODY),
                size = cok_field_u8(L, c, COK_FIELD_ICON_SIZE);
        if (!copy(game, c, slot, SLOT_NEW, true)) return false;
        pair(game, 1, 2);
        text(game, "old", 8, 6);
        text(game, "ready   action", 3, 10);
        text(game, "new", 8, 12);
        text(game, "ready   action", 3, 16);
        uint8_t part = 1, edited = 0;
        bool second = false;
        int key;
        for (;;) {
            pair(game, 1, 4);
            char items[48];
            if (level == 3)
                snprintf(items, sizeof items, "Weapon Body %s Shield Arm Leg Exit",
                         second ? "Face" : "Hair");
            else
                snprintf(items, sizeof items, "%s%s",
                         level == 4
                             ? (cok_field_u8(L, c, COK_FIELD_ICON_SIZE) == 2 ? "Small" : "Large")
                             : "",
                         menus[level]);
            bool special;
            key = cok_camp_menu(game, "", items, false, false, &special);
            if (key < 0) return false;
            if (special) {
                if (parent < 0 && (key == 0 || key == 'E'))
                    return fail(game, "the icon editor's first key tests a level it never "
                                      "set (4def:408b)");
            } else if (level == 1) {
                parent = 1;
                if (key == 'P') level = 2;
                else if ((key == '1' || key == '2') && game->display == COK_DISPLAY_CGA)
                    cok_adventure_notice(game, cok_game_rules(game)->creation.no_cga, 14);
                else if (key == '1' || key == '2') level = 3, second = key == '2';
                else if (key == 'S') level = 4;
            } else if (level == 2) {
                parent = 2;
                if (key == 0 || key == 'E') {
                    level = 1;
                } else {
                    edited = (uint8_t)key;
                    level = 5;
                }
            } else if (level == 3) {
                parent = 3;
                switch (key) {
                case 'W': part = 6; break;
                case 'H': case 'F': part = 4; break;
                case 'S': part = 5; break;
                case 'A': part = 2; break;
                case 'L': part = 3; break;
                default: part = 1; break;
                }
                level = key == 0 || key == 'E' ? 1 : 5;
            } else if (level == 4) {
                if (key == 'L') {
                    cok_field_set8(L, c, COK_FIELD_ICON_SIZE, 2);
                    if (!build(game, character)) return false;
                }
                if (key == 'S') {
                    cok_field_set8(L, c, COK_FIELD_ICON_SIZE, 1);
                    if (!build(game, character)) return false;
                }
                if (key == 'K') {
                    size = cok_field_u8(L, c, COK_FIELD_ICON_SIZE);
                    level = 1;
                    key = ' ';
                } else if (key == 'E' || key == 0) {
                    cok_field_set8(L, c, COK_FIELD_ICON_SIZE, size);
                    level = 1;
                    key = ' ';
                }
                if (!build(game, character)) return false;
            } else if (parent == 2 && (edited == 'H' || edited == 'W')) {
                cok_field part_field = edited == 'H' ? COK_FIELD_ICON_HEAD : COK_FIELD_ICON_BODY;
                uint8_t *field = c + cok_field_offset(L, part_field);
                uint8_t last = edited == 'H' ? cok_game_rules(game)->creation.heads
                                             : cok_game_rules(game)->creation.bodies,
                        kept = edited == 'H' ? head : body;
                if (key == 'P') {
                    *field = *field > 0 ? (uint8_t)(*field - 1) : last;
                } else if (key == 'N') {
                    *field = *field < last ? (uint8_t)(*field + 1) : 0;
                } else if (key == 'K') {
                    if (edited == 'H') head = *field;
                    else body = *field;
                    level = 2;
                    key = ' ';
                } else if (key == 'E' || key == 0) {
                    *field = kept;
                    level = 2;
                    key = ' ';
                }
                if (!build(game, character)) return false;
            } else if (parent == 3) {
                uint8_t *byte = c + cok_field_offset_at(L, COK_FIELD_ICON_COLOURS, part);
                uint8_t low = *byte & 15, high = *byte >> 4;
                if (key == 'N' || key == 'P') {
                    int step = key == 'N' ? 1 : 15;
                    if (second) high = (uint8_t)((high + step) & 15);
                    else low = (uint8_t)((low + step) & 15);
                    *byte = (uint8_t)(low + high * 16);
                } else if (key == 'K') {
                    memcpy(colours, c + cok_field_offset(L, COK_FIELD_ICON_COLOURS),
                           sizeof colours);
                    level = 3;
                    key = ' ';
                } else if (key == 'E' || key == 0) {
                    memcpy(c + cok_field_offset(L, COK_FIELD_ICON_COLOURS), colours,
                           sizeof colours);
                    level = 3;
                    key = ' ';
                }
            }
            /* Else, after a space in Parts, nothing: the original is stuck
             * at this level for good. */
            if (!copy(game, c, cok_field_u8(L, c, COK_FIELD_ICON), SLOT_NEW, true)) return false;
            if (parent == 1 && (key == 0 || key == 'E')) break;
        }
        cok_field_set8(L, c, COK_FIELD_ICON_HEAD, head);
        cok_field_set8(L, c, COK_FIELD_ICON_BODY, body);
        cok_field_set8(L, c, COK_FIELD_ICON_SIZE, size);
        memcpy(c + cok_field_offset(L, COK_FIELD_ICON_COLOURS), colours, sizeof colours);
        if (!copy(game, c, cok_field_u8(L, c, COK_FIELD_ICON), SLOT_NEW, true) ||
            !copy(game, c, SLOT_NEW, cok_field_u8(L, c, COK_FIELD_ICON), false))
            return false;
        cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 67b5:0c7b */
        cok_arena_free_icon(game, SLOT_NEW);
        int answer = cok_camp_yes_no(game, cok_game_rules(game)->creation.icon_ok, 13);
        if (answer < 0) return false;
        if (answer == 'Y') return true;
    }
}

/* The menu of level (dkk 49cb:391b): its items 0 to DS:0589's count, less
 * Face for 1st-color or Hair for 2nd-color (level 3) and the size the icon
 * has (level 4). */
static void level_items(cok_menu_items *items, int level, bool second, uint8_t size)
{
    static const uint8_t last[6] = {7, 4, 2, 7, 3, 3}; /* DS:0589 */
    cok_menu_items_upto(items, last[level]);
    if (level == 3) {
        /* Entries from 3 (2 for 2nd-color) to 6 hold the next index, the
         * last left out (DS:c12e 0xff). */
        for (uint8_t k = second ? 2 : 3; k <= 6; ++k) items->index[k] = (uint8_t)(k + 1);
        items->count = 7;
    } else if (level == 4) {
        for (uint8_t k = size == 2 ? 1 : 0; k <= 2; ++k) items->index[k] = (uint8_t)(k + 1);
        items->count = 3;
    }
}

/* Death Knights' editor (dkk 49cb:3cac): the same screen, its menus by
 * index (6a69:0f1b over DS:35b8 + 0x2e * level, no keypad), each item by
 * its place; special keys, Escape among them, do nothing at all (each
 * branch is under DS:c0f8 clear). Parts' Exit leaves; Head and Weapon
 * (level 2) or a part's colour (level 3) go to Next Prev Keep Exit, whose
 * Keep and Exit go back; Size's Small, Large, Keep and Exit; 1st-color and
 * 2nd-color in CGA mode say "sorry not in CGA" (DS:8c7a 0). Then as
 * Champions': "is this icon ok? ", from Yes, until Yes. */
static bool edit_indexed(cok_adventure *game, cok_character *character)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    const cok_rules_creation *C = &rules->creation;
    uint8_t *c = character->record;
    cok_adventure_screen(game, COK_SCREEN_OPEN); /* 110f:0000 with 0 */
    static const char *const menus[6] = {
        "", "Parts 1st-color 2nd-color Size Exit", "Head Weapon Exit",
        "Weapon Body Hair Face Shield Arm Leg Exit", "Small Large Keep Exit",
        "Next Prev Keep Exit",
    };
    cok_keyboard keys = cok_adventure_keyboard(game);
    /* The colour byte's part ([bp-0xf], -1 while never set): 49cb:3cac
     * sets it only at 3ece-3f0e, so a No to "is this icon ok? " keeps it
     * for the next pass. */
    int part = -1;
    for (;;) {
        if (!build(game, character)) return false;
        int level = 1;
        bool second = false;
        cok_menu_items items;
        level_items(&items, level, second, 0);
        uint8_t colours[6];
        memcpy(colours, c + cok_field_offset(L, COK_FIELD_ICON_COLOURS), sizeof colours);
        uint8_t slot = cok_field_u8(L, c, COK_FIELD_ICON);
        cok_field_set8(L, c, COK_FIELD_ICON, SLOT_NEW);
        bool built = build(game, character);
        cok_field_set8(L, c, COK_FIELD_ICON, slot);
        if (!built) return false;
        uint8_t head = cok_field_u8(L, c, COK_FIELD_ICON_HEAD),
                body = cok_field_u8(L, c, COK_FIELD_ICON_BODY),
                size = cok_field_u8(L, c, COK_FIELD_ICON_SIZE);
        if (!copy(game, c, slot, SLOT_NEW, true)) return false;
        pair(game, 1, 2);
        text(game, "old", 8, 6);
        text(game, "ready   action", 3, 10);
        text(game, "new", 8, 12);
        text(game, "ready   action", 3, 16);
        /* The level the last item was taken at ([bp-0x13]: 1, 2 or 3), the
         * part edited (Head 0, Weapon 1: [bp-0x14]) and which nibble
         * ([bp-0x10]). */
        int from = 0, edited = 0;
        bool done = false;
        while (!done) {
            pair(game, 1, 4);
            cok_adventure_log(game, "menu", menus[level]);
            bool special;
            int key = cok_menu_pick(&game->screen, &game->font, "", menus[level], &items, 13, 15,
                                    10, false, &game->selected, &keys, &special);
            if (key < 0) return false;
            game->menu_special = special; /* DS:c0f8 */
            /* 49cb:391b, the next level's items, where the original calls it. */
            int next = -1;
            if (special) {
                /* Nothing. */
            } else if (level == 1) {
                from = 1;
                if (key == 0) {
                    next = 2;
                } else if (key == 1 || key == 2) {
                    if (game->display == COK_DISPLAY_CGA) {
                        cok_adventure_notice(game, C->no_cga, 14);
                    } else {
                        second = key == 2;
                        next = 3;
                    }
                } else if (key == 3) {
                    next = 4;
                } else if (key == 4) {
                    done = true;
                }
            } else if (level == 2) {
                from = 2;
                if (key == 2) {
                    next = 1;
                } else {
                    next = 5;
                    edited = key;
                }
            } else if (level == 3) {
                from = 3;
                static const int8_t parts[8] = {6, 1, 4, 4, 5, 2, 3, 1};
                next = key == 7 ? 1 : 5;
                if (key >= 0 && key <= 7) part = parts[key];
            } else if (level == 4) {
                if (key == 0 || key == 1) {
                    cok_field_set8(L, c, COK_FIELD_ICON_SIZE, (uint8_t)(key + 1));
                    if (!build(game, character)) return false;
                    next = 4;
                } else if (key == 2) {
                    size = cok_field_u8(L, c, COK_FIELD_ICON_SIZE);
                    next = 1;
                } else if (key == 3) {
                    cok_field_set8(L, c, COK_FIELD_ICON_SIZE, size);
                    next = 1;
                }
                if (!build(game, character)) return false;
            } else if (level == 5 && from == 2 && (edited == 0 || edited == 1)) {
                cok_field field = edited == 0 ? COK_FIELD_ICON_HEAD : COK_FIELD_ICON_BODY;
                uint8_t *value = c + cok_field_offset(L, field);
                uint8_t most = edited == 0 ? C->heads : C->bodies;
                uint8_t *kept = edited == 0 ? &head : &body;
                if (key == 0) {
                    *value = *value < most ? (uint8_t)(*value + 1) : 0;
                } else if (key == 1) {
                    *value = *value == 0 ? most : (uint8_t)(*value - 1);
                } else if (key == 2) {
                    *kept = *value;
                    next = 2;
                } else if (key == 3) {
                    *value = *kept;
                    next = 2;
                }
                if (!build(game, character)) return false;
            } else if (level == 5 && from == 3) {
                /* An index the menu does not give (0xff) leaves the part
                 * as it was: never set at first. */
                if (part < 0 && (key == 0 || key == 1))
                    return fail(game, "a colour's part is a local the icon editor never set "
                                      "(dkk 49cb:3cac's [bp-0xf], read at 410a)");
                if (part < 0) part = 1;
                uint8_t *byte = c + cok_field_offset_at(L, COK_FIELD_ICON_COLOURS, part);
                uint8_t low = *byte & 15, high = *byte >> 4;
                if (key == 0 || key == 1) {
                    int step = key == 0 ? 1 : 15;
                    if (second) high = (uint8_t)((high + step) & 15);
                    else low = (uint8_t)((low + step) & 15);
                    *byte = (uint8_t)(high * 16 + low);
                } else if (key == 2) {
                    memcpy(colours, c + cok_field_offset(L, COK_FIELD_ICON_COLOURS),
                           sizeof colours);
                    next = 3;
                } else if (key == 3) {
                    memcpy(c + cok_field_offset(L, COK_FIELD_ICON_COLOURS), colours,
                           sizeof colours);
                    next = 3;
                }
            }
            if (next >= 0) {
                level = next;
                level_items(&items, level, second, cok_field_u8(L, c, COK_FIELD_ICON_SIZE));
            }
            if (!copy(game, c, cok_field_u8(L, c, COK_FIELD_ICON), SLOT_NEW, true)) return false;
        }
        cok_field_set8(L, c, COK_FIELD_ICON_HEAD, head);
        cok_field_set8(L, c, COK_FIELD_ICON_BODY, body);
        cok_field_set8(L, c, COK_FIELD_ICON_SIZE, size);
        memcpy(c + cok_field_offset(L, COK_FIELD_ICON_COLOURS), colours, sizeof colours);
        if (!copy(game, c, cok_field_u8(L, c, COK_FIELD_ICON), SLOT_NEW, true) ||
            !copy(game, c, SLOT_NEW, cok_field_u8(L, c, COK_FIELD_ICON), false))
            return false;
        cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 6a69:145d */
        cok_arena_free_icon(game, SLOT_NEW);                     /* 7090:00ee */
        int answer = cok_camp_ask(game, C->icon_ok, 13, C->icon_ok_yes);
        if (answer < 0) return false;
        if (answer == 'Y') return true;
    }
}
